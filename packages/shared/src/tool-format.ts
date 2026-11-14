// Turns a Claude Code tool call into the text of a permission card or status
// line, plus a risk hint. Shared by the hook dispatcher (permission and
// PostToolUse status) and anything else that describes tool calls.

import { LIMITS, type Risk } from "./protocol.js";
import { sanitizeAscii, utf8ByteLength, utf8Truncate } from "./text.js";

export interface FormattedToolCall {
  tool: string;
  summary: string;
  details?: string;
  risk: Risk;
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
  },
  {
    id: "git-push-force",
    risk: "high",
    target: "bash",
    pattern: /\bgit\s+push\b[^;&|\n]*\s(?:--force(?:-with-lease)?(?:=\S*)?|-f)(?=\s|$)/,
    description: "git push --force / -f / --force-with-lease",
  },
  {
    id: "git-reset-hard",
    risk: "high",
    target: "bash",
    pattern: /\bgit\s+reset\b[^;&|\n]*\s--hard\b/,
    description: "git reset --hard",
  },
  {
    id: "git-clean-force",
    risk: "high",
    target: "bash",
    pattern: /\bgit\s+clean\b[^;&|\n]*\s(?:-[a-zA-Z]*f[a-zA-Z]*|--force)(?=\s|$)/,
    description: "git clean -f",
  },
  { id: "sudo", risk: "high", target: "bash", pattern: /\bsudo\b/, description: "sudo" },
  {
    id: "pipe-to-shell",
    risk: "high",
    target: "bash",
    pattern: /\b(?:curl|wget)\b[^;&\n]*\|\s*(?:sudo\s+)?(?:sh|bash|zsh)\b/,
    description: "curl or wget piped into a shell",
  },
  { id: "dd", risk: "high", target: "bash", pattern: /\bdd\b[^;&|\n]*\bif=/, description: "dd if=" },
  { id: "mkfs", risk: "high", target: "bash", pattern: /\bmkfs(?:\.[a-z0-9]+)?\b/, description: "mkfs" },
  {
    id: "sql-drop",
    risk: "high",
    target: "bash",
    pattern: /\bdrop\s+(?:table|database)\b/i,
    description: "DROP TABLE / DROP DATABASE",
  },
  {
    id: "package-publish",
    risk: "high",
    target: "bash",
    pattern: /\b(?:npm|pnpm|yarn)\s+publish\b/,
    description: "npm / pnpm / yarn publish",
  },
  {
    id: "chmod-recursive-777",
    risk: "high",
    target: "bash",
    pattern: /\bchmod\b(?=[^;&|\n]*\s-[a-zA-Z]*R)(?=[^;&|\n]*\s0?777(?=\s|$))/,
    description: "chmod -R 777",
  },
  {
    id: "redirect-to-disk",
    risk: "high",
    target: "bash",
    pattern: />\s*\/dev\/sd[a-z]/,
    description: "redirect to /dev/sd*",
  },
  {
    id: "write-ssh",
    risk: "high",
    target: "path",
    pattern: /(?:^|\/)\.ssh(?:\/|$)/,
    description: "target under ~/.ssh",
  },
  { id: "write-etc", risk: "high", target: "path", pattern: /^\/etc\//, description: "target under /etc/" },
  {
    id: "write-git-hooks",
    risk: "high",
    target: "path",
    pattern: /(?:^|\/)\.git\/hooks(?:\/|$)/,
    description: "target under .git/hooks",
  },
  { id: "git-push", risk: "med", target: "bash", pattern: /\bgit\s+push\b/, description: "git push" },
  { id: "rm", risk: "med", target: "bash", pattern: /\brm\b/, description: "rm" },
  {
    id: "package-install",
    risk: "med",
    target: "bash",
    pattern: /\b(?:npm|pnpm|yarn|pip3?|cargo|apt(?:-get)?|brew)\s+(?:install|add|i)\b/,
    description: "package install",
  },
  { id: "docker", risk: "med", target: "bash", pattern: /\bdocker\b/, description: "docker" },
  {
    id: "network",
    risk: "med",
    target: "bash",
    pattern: /\b(?:curl|wget|ssh|scp|nc)\b/,
    description: "curl / wget / ssh / scp / nc",
  },
  { id: "kill", risk: "med", target: "bash", pattern: /\b(?:kill|pkill|killall)\b/, description: "kill" },
  { id: "chmod-chown", risk: "med", target: "bash", pattern: /\b(?:chmod|chown)\b/, description: "chmod / chown" },
  {
    id: "write-env",
    risk: "med",
    target: "path",
    pattern: /(?:^|\/)\.env[^/]*$/,
    description: "target is a .env* file",
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

function bashRisk(command: string): Risk {
  return firstMatch("bash", command)?.risk ?? "low";
}

function pathRisk(target: ResolvedPath | undefined): Risk {
  if (!target) return "med";
  const rule = firstMatch("path", target.absolute ?? target.display);
  if (rule) return rule.risk;
  return target.insideCwd ? "low" : "med";
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

function finish(tool: string, summary: string, details: string | undefined, risk: Risk): FormattedToolCall {
  const out: FormattedToolCall = {
    tool: utf8Truncate(sanitizeAscii(clean(tool)), LIMITS.tool),
    summary: utf8Truncate(sanitizeAscii(clean(summary).replace(/\n/g, " ")), LIMITS.summary),
    risk,
  };
  if (details !== undefined) {
    const text = utf8Truncate(sanitizeAscii(clean(details)), LIMITS.details);
    if (text !== "") out.details = text;
  }
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

function formatMcp(name: string, input: unknown): FormattedToolCall {
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
  return finish(display, summary, details, "med");
}

/**
 * Formats a tool call for a permission card or status line. Never throws:
 * any value is accepted for every argument.
 */
export function formatToolCall(toolName: unknown, toolInput: unknown, cwd?: string): FormattedToolCall {
  try {
    return formatUnsafe(toolName, toolInput, typeof cwd === "string" && cwd !== "" ? cwd : undefined);
  } catch {
    return finish(typeof toolName === "string" && toolName !== "" ? toolName : "Unknown tool", "(unreadable input)", undefined, "med");
  }
}

function formatUnsafe(toolName: unknown, toolInput: unknown, cwd: string | undefined): FormattedToolCall {
  if (typeof toolName !== "string" || toolName === "") {
    return finish("Unknown tool", compactJson(toolInput ?? null), undefined, "med");
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
      return finish("Bash", firstLine(command) || "(empty command)", details, bashRisk(command));
    }
    case "Edit": {
      const path = pathOf("file_path");
      const oldText = clean(str(input.old_string) ?? "");
      const newText = clean(str(input.new_string) ?? "");
      const details = diffDetails(oldText, newText, lines(oldText).length, lines(newText).length);
      return finish("Edit", `Edit ${shown(path)}`, details, pathRisk(path));
    }
    case "MultiEdit": {
      const path = pathOf("file_path");
      const edits = Array.isArray(input.edits) ? input.edits.map(asInput) : [];
      const oldTexts = edits.map((edit) => clean(str(edit.old_string) ?? ""));
      const newTexts = edits.map((edit) => clean(str(edit.new_string) ?? ""));
      const count = (texts: string[]) => texts.reduce((sum, text) => sum + lines(text).length, 0);
      const details = diffDetails(oldTexts[0] ?? "", newTexts[0] ?? "", count(oldTexts), count(newTexts));
      const noun = edits.length === 1 ? "edit" : "edits";
      return finish("MultiEdit", `Edit ${shown(path)} (${edits.length} ${noun})`, details, pathRisk(path));
    }
    case "Write": {
      const path = pathOf("file_path");
      const content = clean(str(input.content) ?? "");
      const contentLines = lines(content);
      const noun = contentLines.length === 1 ? "line" : "lines";
      const details = contentLines.slice(0, WRITE_PREVIEW_LINES).join("\n");
      return finish("Write", `Write ${shown(path)} (${contentLines.length} ${noun})`, details, pathRisk(path));
    }
    case "Read":
      return finish("Read", `Read ${shown(pathOf("file_path"))}`, undefined, "low");
    case "NotebookEdit": {
      const path = pathOf("notebook_path");
      const source = str(input.new_source);
      const details = source === undefined ? undefined : lines(clean(source)).slice(0, WRITE_PREVIEW_LINES).join("\n");
      return finish("NotebookEdit", `Edit notebook ${shown(path)}`, details, pathRisk(path));
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
      return finish("WebFetch", `Fetch ${host || "(no url)"}`, prompt ? `${url}\n${prompt}` : url, "low");
    }
    case "WebSearch":
      return finish("WebSearch", `Search: ${str(input.query) ?? ""}`, undefined, "low");
    case "Task":
    case "Agent": {
      const type = str(input.subagent_type);
      const description = str(input.description) ?? "";
      const summary = type ? `Agent ${type}: ${description}` : `Agent: ${description}`;
      const prompt = str(input.prompt);
      return finish(toolName, summary, prompt, "low");
    }
  }
  if (toolName.startsWith("mcp__")) return formatMcp(toolName, toolInput);
  const risk: Risk = KNOWN_LOW_TOOLS.has(toolName) ? "low" : "med";
  return finish(toolName, compactJson(toolInput ?? {}), undefined, risk);
}
