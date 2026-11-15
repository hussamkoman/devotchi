// Turns a Claude Code tool call into the text of a permission card or status
// line, plus a risk hint. Shared by the hook dispatcher (permission and
// PostToolUse status) and anything else that describes tool calls.

import { LIMITS, type Risk } from "./protocol.js";
import { summarisePlan } from "./requests.js";
import { sanitizeAscii, utf8ByteLength, utf8Truncate } from "./text.js";

export interface FormattedToolCall {
  tool: string;
  summary: string;
  /** Card text for phase-1 consumers. */
  details?: string;
  risk: Risk;
  /** Protocol 14.2: the exact command, path and change, or the input as JSON; ASCII, at most 600 bytes. */
  cmd: string;
  /** `cmd` is not the complete input: it was shortened, or a character had to be replaced. */
  cut?: true;
  /** Claude's own description of the call (Bash `description`). */
  desc?: string;
  /** Why the risk is med or high, at most 40 bytes. */
  reason?: string;
}

const PATH_MAX_BYTES = 60;
const PATH_TAIL_BYTES = 57;
const DIFF_PREVIEW_LINES = 6;
const WRITE_PREVIEW_LINES = 8;
const MCP_SUMMARY_ARGS = 3;
const MCP_VALUE_BYTES = 40;

// --- Risk rules ---

export interface RiskRule {
  id: string;
  risk: Exclude<Risk, "low">;
  /** `bash` rules match the command; `path` rules match a Write/Edit target. */
  target: "bash" | "path";
  pattern: RegExp;
  description: string;
  /** Shown on a card with `decide` (protocol 14.2), at most 40 bytes. */
  reason: string;
}

// "[^;&|\n]*" keeps a flag match inside one simple command, so the flag of a
// later command in a pipeline or list is not attributed to an earlier one.
export const RISK_RULES: readonly RiskRule[] = [
  {
    id: "rm-recursive-force",
    risk: "high",
    target: "bash",
    pattern: /\brm\b(?=[^;&|\n]*\s(?:-[a-zA-Z]*[rRf][a-zA-Z]*|--recursive|--force)(?=\s|$))/,
    description: "rm with -r or -f",
    reason: "rm -r/-f: deletes files for good",
  },
  {
    id: "git-push-force",
    risk: "high",
    target: "bash",
    pattern: /\bgit\s+push\b[^;&|\n]*\s(?:--force(?:-with-lease)?(?:=\S*)?|-f)(?=\s|$)/,
    description: "git push --force / -f / --force-with-lease",
    reason: "Force push: rewrites the remote branch",
  },
  {
    id: "git-reset-hard",
    risk: "high",
    target: "bash",
    pattern: /\bgit\s+reset\b[^;&|\n]*\s--hard\b/,
    description: "git reset --hard",
    reason: "Hard reset: discards local changes",
  },
  {
    id: "git-clean-force",
    risk: "high",
    target: "bash",
    pattern: /\bgit\s+clean\b[^;&|\n]*\s(?:-[a-zA-Z]*f[a-zA-Z]*|--force)(?=\s|$)/,
    description: "git clean -f",
    reason: "git clean: deletes untracked files",
  },
  {
    id: "sudo",
    risk: "high",
    target: "bash",
    pattern: /\bsudo\b/,
    description: "sudo",
    reason: "sudo: runs as root",
  },
  {
    id: "pipe-to-shell",
    risk: "high",
    target: "bash",
    pattern: /\b(?:curl|wget)\b[^;&\n]*\|\s*(?:sudo\s+)?(?:sh|bash|zsh)\b/,
    description: "curl or wget piped into a shell",
    reason: "Runs a downloaded script",
  },
  {
    id: "dd",
    risk: "high",
    target: "bash",
    pattern: /\bdd\b[^;&|\n]*\bif=/,
    description: "dd if=",
    reason: "dd: writes raw data to a disk or file",
  },
  {
    id: "mkfs",
    risk: "high",
    target: "bash",
    pattern: /\bmkfs(?:\.[a-z0-9]+)?\b/,
    description: "mkfs",
    reason: "mkfs: formats a disk",
  },
  {
    id: "sql-drop",
    risk: "high",
    target: "bash",
    pattern: /\bdrop\s+(?:table|database)\b/i,
    description: "DROP TABLE / DROP DATABASE",
    reason: "DROP: deletes a table or database",
  },
  {
    id: "package-publish",
    risk: "high",
    target: "bash",
    pattern: /\b(?:npm|pnpm|yarn)\s+publish\b/,
    description: "npm / pnpm / yarn publish",
    reason: "Publishes a package",
  },
  {
    id: "chmod-recursive-777",
    risk: "high",
    target: "bash",
    pattern: /\bchmod\b(?=[^;&|\n]*\s-[a-zA-Z]*R)(?=[^;&|\n]*\s0?777(?=\s|$))/,
    description: "chmod -R 777",
    reason: "chmod -R 777: opens files to everyone",
  },
  {
    id: "redirect-to-disk",
    risk: "high",
    target: "bash",
    pattern: />\s*\/dev\/sd[a-z]/,
    description: "redirect to /dev/sd*",
    reason: "Writes directly to a disk",
  },
  {
    id: "write-ssh",
    risk: "high",
    target: "path",
    pattern: /(?:^|\/)\.ssh(?:\/|$)/,
    description: "target under ~/.ssh",
    reason: "Changes SSH keys or config",
  },
  {
    id: "write-etc",
    risk: "high",
    target: "path",
    pattern: /^\/etc\//,
    description: "target under /etc/",
    reason: "Changes system config in /etc",
  },
  {
    id: "write-git-hooks",
    risk: "high",
    target: "path",
    pattern: /(?:^|\/)\.git\/hooks(?:\/|$)/,
    description: "target under .git/hooks",
    reason: "Changes git hooks, which run code",
  },
  {
    id: "git-push",
    risk: "med",
    target: "bash",
    pattern: /\bgit\s+push\b/,
    description: "git push",
    reason: "Pushes to the remote",
  },
  {
    id: "rm",
    risk: "med",
    target: "bash",
    pattern: /\brm\b/,
    description: "rm",
    reason: "rm: deletes files",
  },
  {
    id: "package-install",
    risk: "med",
    target: "bash",
    pattern: /\b(?:npm|pnpm|yarn|pip3?|cargo|apt(?:-get)?|brew)\s+(?:install|add|i)\b/,
    description: "package install",
    reason: "Installs packages",
  },
  {
    id: "docker",
    risk: "med",
    target: "bash",
    pattern: /\bdocker\b/,
    description: "docker",
    reason: "docker: controls containers",
  },
  {
    id: "network",
    risk: "med",
    target: "bash",
    pattern: /\b(?:curl|wget|ssh|scp|nc)\b/,
    description: "curl / wget / ssh / scp / nc",
    reason: "Network access",
  },
  {
    id: "kill",
    risk: "med",
    target: "bash",
    pattern: /\b(?:kill|pkill|killall)\b/,
    description: "kill",
    reason: "Stops processes",
  },
  {
    id: "chmod-chown",
    risk: "med",
    target: "bash",
    pattern: /\b(?:chmod|chown)\b/,
    description: "chmod / chown",
    reason: "Changes file permissions",
  },
  {
    id: "write-env",
    risk: "med",
    target: "path",
    pattern: /(?:^|\/)\.env[^/]*$/,
    description: "target is a .env* file",
    reason: "Changes a .env file (secrets)",
  },
];

const KNOWN_LOW_TOOLS = new Set([
  "Read",
  "Glob",
  "Grep",
  "LS",
  "WebFetch",
  "WebSearch",
  "Task",
  "Agent",
  "TodoWrite",
  "NotebookRead",
]);

/** Protocol 8.5: Devotchi's own MCP tools get no PostToolUse status. */
export function isOwnMcpTool(toolName: unknown): boolean {
  return typeof toolName === "string" && /^mcp__devotchi__/.test(toolName);
}

function firstMatch(target: RiskRule["target"], text: string): RiskRule | undefined {
  return RISK_RULES.find((rule) => rule.target === target && rule.pattern.test(text));
}

interface Assessment {
  risk: Risk;
  reason?: string;
}

function bashRisk(command: string): Assessment {
  const rule = firstMatch("bash", command);
  return rule ? { risk: rule.risk, reason: rule.reason } : { risk: "low" };
}

function pathRisk(target: ResolvedPath | undefined): Assessment {
  if (!target) return { risk: "med", reason: "No file path given" };
  const rule = firstMatch("path", target.absolute ?? target.display);
  if (rule) return { risk: rule.risk, reason: rule.reason };
  return target.insideCwd ? { risk: "low" } : { risk: "med", reason: "Path outside the project" };
}

// --- Paths (POSIX semantics; Windows separators are normalised to "/") ---

interface ResolvedPath {
  /** Normalised absolute path, when it can be determined. */
  absolute?: string;
  display: string;
  insideCwd: boolean;
}

/** "/" or a drive root such as "C:/", or "" for a relative path. */
function rootOf(path: string): string {
  if (path.startsWith("/")) return "/";
  const drive = /^[A-Za-z]:\//.exec(path);
  return drive ? drive[0] : "";
}

function normalise(path: string): string {
  const root = rootOf(path);
  const parts: string[] = [];
  for (const part of path.slice(root.length).split("/")) {
    if (part === "" || part === ".") continue;
    if (part === "..") {
      if (parts.length > 0 && parts[parts.length - 1] !== "..") parts.pop();
      else if (!root) parts.push("..");
      continue;
    }
    parts.push(part);
  }
  return root + parts.join("/");
}

function resolvePath(raw: string, cwd: string | undefined): ResolvedPath {
  const path = raw.replace(/\\/g, "/");
  const base = cwd ? normalise(cwd.replace(/\\/g, "/")) : undefined;
  const isAbsolute = rootOf(path) !== "";
  if (!base || rootOf(base) === "") {
    const normal = normalise(path);
    return {
      absolute: isAbsolute ? normal : undefined,
      display: normal || ".",
      insideCwd: !isAbsolute && !normal.startsWith(".."),
    };
  }
  const absolute = normalise(isAbsolute ? path : `${base}/${path}`);
  const prefix = base.endsWith("/") ? base : `${base}/`;
  if (absolute === base) return { absolute, display: ".", insideCwd: true };
  if (absolute.startsWith(prefix)) return { absolute, display: absolute.slice(prefix.length), insideCwd: true };
  return { absolute, display: absolute, insideCwd: false };
}

/** True when the absolute `path` is `cwd` or lies below it; false without an absolute `cwd`. */
export function isInsideCwd(path: string, cwd: string | undefined): boolean {
  if (cwd === undefined || cwd === "" || rootOf(cwd.replace(/\\/g, "/")) === "") return false;
  return resolvePath(path, cwd).insideCwd;
}

function shortenPath(display: string): string {
  const ascii = sanitizeAscii(display);
  if (utf8ByteLength(ascii) <= PATH_MAX_BYTES) return ascii;
  return `...${ascii.slice(ascii.length - PATH_TAIL_BYTES)}`;
}

// --- Text helpers ---

function str(value: unknown): string | undefined {
  return typeof value === "string" ? value : undefined;
}

function lines(text: string): string[] {
  if (text === "") return [];
  const all = text.split("\n");
  if (all[all.length - 1] === "") all.pop();
  return all;
}

function firstLine(text: string): string {
  return lines(text.replace(/^\s+/, ""))[0] ?? "";
}

/** Tabs and CR would each become "?" on the device. */
function clean(text: string): string {
  return text.replace(/\r\n?/g, "\n").replace(/\t/g, "  ");
}

function compactJson(value: unknown): string {
  try {
    return JSON.stringify(value) ?? String(value);
  } catch {
    return String(value);
  }
}

function asInput(toolInput: unknown): Record<string, unknown> {
  return typeof toolInput === "object" && toolInput !== null && !Array.isArray(toolInput)
    ? (toolInput as Record<string, unknown>)
    : {};
}

/** A formatter's result before the protocol limits are applied. */
interface RawCall extends Assessment {
  tool: string;
  summary: string;
  details?: string;
  cmd: string;
  desc?: string;
}

const NON_ASCII = /[^\x20-\x7e\n]/;

function finish(raw: RawCall): FormattedToolCall {
  const out: FormattedToolCall = {
    tool: utf8Truncate(sanitizeAscii(clean(raw.tool)), LIMITS.tool),
    summary: utf8Truncate(sanitizeAscii(clean(raw.summary).replace(/\n/g, " ")), LIMITS.summary),
    risk: raw.risk,
    cmd: "",
  };
  if (raw.details !== undefined) {
    const text = utf8Truncate(sanitizeAscii(clean(raw.details)), LIMITS.details);
    if (text !== "") out.details = text;
  }
  const cmd = clean(raw.cmd);
  const ascii = sanitizeAscii(cmd);
  out.cmd = utf8Truncate(ascii, LIMITS.cmd);
  if (NON_ASCII.test(cmd) || out.cmd.length < ascii.length) out.cut = true;
  if (raw.desc !== undefined) {
    const desc = utf8Truncate(sanitizeAscii(clean(raw.desc).replace(/\n/g, " ")).trim(), LIMITS.desc);
    if (desc !== "") out.desc = desc;
  }
  if (raw.reason !== undefined && raw.risk !== "low") out.reason = utf8Truncate(raw.reason, LIMITS.riskReason);
  return out;
}

// --- Formatters ---

function diffDetails(oldText: string, newText: string, oldCount: number, newCount: number): string {
  const removed = lines(oldText)
    .slice(0, DIFF_PREVIEW_LINES)
    .map((line) => `- ${line}`);
  const added = lines(newText)
    .slice(0, DIFF_PREVIEW_LINES)
    .map((line) => `+ ${line}`);
  return [`-${oldCount} +${newCount} lines`, ...removed, ...added].join("\n");
}

/** Protocol 14.2 `cmd` of a file change: the path, `+A -B lines`, then every removed and added line. */
function changeCmd(path: ResolvedPath | undefined, changes: { oldText: string; newText: string }[]): string {
  const removed = changes.flatMap((change) => lines(change.oldText));
  const added = changes.flatMap((change) => lines(change.newText));
  const body = changes.flatMap((change) => [
    ...lines(change.oldText).map((line) => `- ${line}`),
    ...lines(change.newText).map((line) => `+ ${line}`),
  ]);
  return [path ? path.display : "(no path)", `+${added.length} -${removed.length} lines`, ...body].join("\n");
}

function formatMcp(name: string, input: unknown): RawCall {
  const rest = name.slice("mcp__".length);
  const split = rest.indexOf("__");
  const server = split >= 0 ? rest.slice(0, split) : rest;
  const tool = split >= 0 ? rest.slice(split + 2) : "";
  const display = tool ? `${server}: ${tool}` : server;
  const args = asInput(input);
  const pairs = Object.entries(args)
    .slice(0, MCP_SUMMARY_ARGS)
    .map(([key, value]) => `${key}=${utf8Truncate(compactJson(value), MCP_VALUE_BYTES)}`);
  const summary = pairs.length > 0 ? pairs.join(" ") : display;
  const details = Object.keys(args).length > 0 ? compactJson(args) : undefined;
  return { tool: display, summary, details, risk: "med", reason: "Tool of an MCP server", cmd: compactJson(args) };
}

/**
 * Formats a tool call for a permission card or status line. Never throws:
 * any value is accepted for every argument.
 */
export function formatToolCall(toolName: unknown, toolInput: unknown, cwd?: string): FormattedToolCall {
  try {
    return finish(formatUnsafe(toolName, toolInput, typeof cwd === "string" && cwd !== "" ? cwd : undefined));
  } catch {
    const tool = typeof toolName === "string" && toolName !== "" ? toolName : "Unknown tool";
    return finish({ tool, summary: "(unreadable input)", risk: "med", reason: "Unreadable input", cmd: "(unreadable input)" });
  }
}

function formatUnsafe(toolName: unknown, toolInput: unknown, cwd: string | undefined): RawCall {
  const json = compactJson(toolInput ?? {});
  if (typeof toolName !== "string" || toolName === "") {
    return { tool: "Unknown tool", summary: compactJson(toolInput ?? null), risk: "med", reason: "Unknown tool", cmd: json };
  }
  const input = asInput(toolInput);
  const pathOf = (key: string) => {
    const raw = str(input[key]);
    return raw === undefined || raw === "" ? undefined : resolvePath(raw, cwd);
  };
  const shown = (path: ResolvedPath | undefined) => (path ? shortenPath(path.display) : "(no path)");

  switch (toolName) {
    case "Bash": {
      const command = clean(str(input.command) ?? "");
      const description = str(input.description);
      const details = `${description ? `${description}\n` : ""}$ ${command}`;
      return {
        tool: "Bash",
        summary: firstLine(command) || "(empty command)",
        details,
        ...bashRisk(command),
        cmd: command,
        desc: description,
      };
    }
    case "Edit": {
      const path = pathOf("file_path");
      const oldText = clean(str(input.old_string) ?? "");
      const newText = clean(str(input.new_string) ?? "");
      const details = diffDetails(oldText, newText, lines(oldText).length, lines(newText).length);
      return {
        tool: "Edit",
        summary: `Edit ${shown(path)}`,
        details,
        ...pathRisk(path),
        cmd: changeCmd(path, [{ oldText, newText }]),
      };
    }
    case "MultiEdit": {
      const path = pathOf("file_path");
      const edits = Array.isArray(input.edits) ? input.edits.map(asInput) : [];
      const changes = edits.map((edit) => ({
        oldText: clean(str(edit.old_string) ?? ""),
        newText: clean(str(edit.new_string) ?? ""),
      }));
      const count = (texts: string[]) => texts.reduce((sum, text) => sum + lines(text).length, 0);
      const details = diffDetails(
        changes[0]?.oldText ?? "",
        changes[0]?.newText ?? "",
        count(changes.map((c) => c.oldText)),
        count(changes.map((c) => c.newText)),
      );
      const noun = edits.length === 1 ? "edit" : "edits";
      return {
        tool: "MultiEdit",
        summary: `Edit ${shown(path)} (${edits.length} ${noun})`,
        details,
        ...pathRisk(path),
        cmd: changeCmd(path, changes),
      };
    }
    case "Write": {
      const path = pathOf("file_path");
      const content = clean(str(input.content) ?? "");
      const contentLines = lines(content);
      const noun = contentLines.length === 1 ? "line" : "lines";
      const details = contentLines.slice(0, WRITE_PREVIEW_LINES).join("\n");
      return {
        tool: "Write",
        summary: `Write ${shown(path)} (${contentLines.length} ${noun})`,
        details,
        ...pathRisk(path),
        cmd: changeCmd(path, [{ oldText: "", newText: content }]),
      };
    }
    case "Read":
      return { tool: "Read", summary: `Read ${shown(pathOf("file_path"))}`, risk: "low", cmd: json };
    case "NotebookEdit": {
      const path = pathOf("notebook_path");
      const source = str(input.new_source);
      const details = source === undefined ? undefined : lines(clean(source)).slice(0, WRITE_PREVIEW_LINES).join("\n");
      return {
        tool: "NotebookEdit",
        summary: `Edit notebook ${shown(path)}`,
        details,
        ...pathRisk(path),
        cmd: changeCmd(path, [{ oldText: "", newText: clean(source ?? "") }]),
      };
    }
    case "WebFetch": {
      const url = str(input.url) ?? "";
      let host = url;
      try {
        host = new URL(url).host || url;
      } catch {
        // An unparsable URL is shown as given.
      }
      const prompt = firstLine(str(input.prompt) ?? "");
      const details = prompt ? `${url}\n${prompt}` : url;
      return { tool: "WebFetch", summary: `Fetch ${host || "(no url)"}`, details, risk: "low", cmd: json };
    }
    case "WebSearch":
      return { tool: "WebSearch", summary: `Search: ${str(input.query) ?? ""}`, risk: "low", cmd: json };
    case "Task":
    case "Agent": {
      const type = str(input.subagent_type);
      const description = str(input.description) ?? "";
      const summary = type ? `Agent ${type}: ${description}` : `Agent: ${description}`;
      return { tool: toolName, summary, details: str(input.prompt), risk: "low", cmd: json };
    }
    case "AskUserQuestion": {
      const questions = Array.isArray(input.questions) ? input.questions.map(asInput) : [];
      const first = firstLine(clean(str(questions[0]?.question) ?? ""));
      const more = questions.length > 1 ? ` (+${questions.length - 1} more)` : "";
      return { tool: "AskUserQuestion", summary: `${first || "Question"}${more}`, risk: "low", cmd: json };
    }
    case "ExitPlanMode": {
      const plan = str(input.plan);
      const title = plan === undefined ? "" : summarisePlan(plan).title;
      return { tool: "ExitPlanMode", summary: title || "Plan", risk: "low", cmd: json };
    }
  }
  if (toolName.startsWith("mcp__")) return formatMcp(toolName, toolInput);
  if (KNOWN_LOW_TOOLS.has(toolName)) return { tool: toolName, summary: json, risk: "low", cmd: json };
  return { tool: toolName, summary: json, risk: "med", reason: "Not a built-in tool", cmd: json };
}
