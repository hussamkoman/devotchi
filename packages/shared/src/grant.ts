// "Allow for this session" (protocol 14.2): whether a permission request may
// offer it, and the rule it adds. The rule always comes word for word from
// Claude Code's own suggestion; nothing here composes or widens one.

import { GRANT_RULE_MIN_BYTES, LIMITS, type GrantMatch, type Risk } from "./protocol.js";
import { isInsideCwd } from "./tool-format.js";
import { utf8ByteLength } from "./text.js";

export interface SessionPrefixFamily {
  name: string;
  programs: readonly string[];
  /** Second words of the rule. */
  words: readonly string[];
  /** When set, the rule has a third word (a script name) matching it. */
  script?: RegExp;
}

/**
 * The only prefix rules a session grant may carry, each exactly
 * `<program> <word>` or `<program> run <script>`. Commands whose arguments
 * can name another program to run never belong here.
 */
export const SESSION_PREFIX_FAMILIES: readonly SessionPrefixFamily[] = [
  {
    name: "Package scripts",
    programs: ["npm", "pnpm", "yarn", "bun"],
    words: ["test", "lint", "build", "typecheck", "check", "format"],
  },
  { name: "Named script", programs: ["npm", "pnpm", "yarn", "bun"], words: ["run"], script: /^[A-Za-z0-9:_-]{1,40}$/ },
  { name: "Cargo", programs: ["cargo"], words: ["test", "build", "check", "clippy", "fmt"] },
  { name: "Go", programs: ["go"], words: ["test", "build", "vet"] },
  { name: "Git, read-only", programs: ["git"], words: ["status", "diff", "log", "show"] },
];

/** First words that run other code given as arguments (protocol 14.2 rule 6). */
export const GRANT_DENIED_FIRST_WORDS: readonly string[] = [
  "sh", "bash", "zsh", "fish", "dash", "ksh", "csh", "node", "deno", "python", "python2", "python3", "perl", "ruby",
  "php", "lua", "npx", "pnpx", "bunx", "uvx", "eval", "exec", "source", ".", "sudo", "doas", "su", "env", "xargs",
  "ssh", "timeout", "nice", "nohup", "time", "command", "builtin", "stdbuf", "ionice", "chrt", "taskset", "setsid",
  "script", "watch", "chroot", "nix", "nix-shell", "flatpak", "awk", "gawk", "sed", "find", "make",
];

/** First word -> second words that make it run another program. */
const DENIED_SECOND_WORDS: ReadonlyMap<string, readonly string[]> = new Map<string, readonly string[]>([
  ...["npm", "pnpm", "yarn", "bun"].map((p) => [p, ["exec", "dlx", "x", "create"]] as const),
  ...["uv", "poetry", "pipx", "conda", "pipenv", "hatch", "pdm"].map((p) => [p, ["run"]] as const),
  ...["docker", "podman", "kubectl", "nerdctl"].map((p) => [p, ["run", "exec"]] as const),
  ["go", ["run"]],
  ["cargo", ["run"]],
  ["git", ["-c", "-C"]],
]);

const PACKAGE_SCRIPT_WORDS = SESSION_PREFIX_FAMILIES[0]!.words;

export interface SessionGrant {
  /** What the consumer shows word for word. */
  rule: string;
  match: GrantMatch;
  /** The suggestion's `ruleContent`, unchanged, for the hook reply. */
  ruleContent: string;
}

type Obj = Record<string, unknown>;
const isObj = (value: unknown): value is Obj => typeof value === "object" && value !== null && !Array.isArray(value);

const PRINTABLE_ASCII = /^[\x20-\x7e]*$/;
const ASSIGNMENT = /^[A-Za-z_][A-Za-z0-9_]*=/;

/** The single usable `addRules` suggestion (rule 3), or null. */
function suggestedRule(suggestions: unknown): string | null {
  if (!Array.isArray(suggestions)) return null;
  const addRules = suggestions.filter((entry) => isObj(entry) && entry.type === "addRules") as Obj[];
  if (addRules.length !== 1) return null;
  const entry = addRules[0]!;
  if (entry.behavior !== "allow" || !Array.isArray(entry.rules) || entry.rules.length !== 1) return null;
  const rule = entry.rules[0] as unknown;
  if (!isObj(rule) || rule.toolName !== "Bash" || typeof rule.ruleContent !== "string") return null;
  return rule.ruleContent;
}

function isAllowlistedPrefix(rule: string): boolean {
  const words = rule.split(" ");
  return SESSION_PREFIX_FAMILIES.some((family) => {
    if (!family.programs.includes(words[0]!) || !family.words.includes(words[1] ?? "")) return false;
    if (family.script) return words.length === 3 && family.script.test(words[2]!);
    return words.length === 2;
  });
}

/** Rule 6. Leading `NAME=value` words are skipped: the program is what follows them. */
function deniedProgram(rule: string): boolean {
  const words = rule.split(" ").filter((word) => word !== "");
  while (words.length > 0 && ASSIGNMENT.test(words[0]!)) words.shift();
  if (words.length === 0) return true;
  const first = words[0]!.slice(words[0]!.lastIndexOf("/") + 1);
  const second = words[1] ?? "";
  if (GRANT_DENIED_FIRST_WORDS.includes(first)) return true;
  if (DENIED_SECOND_WORDS.get(first)?.includes(second)) return true;
  if (first === "git" && second.startsWith("--config-env")) return true;
  if (first === "bun" && second !== "run" && !PACKAGE_SCRIPT_WORDS.includes(second)) return true;
  return false;
}

/** Rule 8: no `..` segment, no `~`, every absolute path inside `cwd`. */
function pathsInsideProject(rule: string, cwd: string | undefined): boolean {
  if (rule.includes("~")) return false;
  // Quotes are dropped first: the shell removes them, so `"/etc/passwd"` names /etc/passwd.
  for (const word of rule.split(" ").map((w) => w.replace(/["']/g, ""))) {
    if (word.split(/[/=]/).includes("..")) return false;
    const candidates = [word, ...word.split("=").slice(1)];
    for (const candidate of candidates) {
      if (candidate.startsWith("/") && !isInsideCwd(candidate, cwd)) return false;
    }
  }
  return true;
}

/**
 * Protocol 14.2 rules 1-8: the grant "Allow for this session" may offer, or
 * null when the request must not offer one.
 */
export function deriveGrant(
  toolName: unknown,
  toolInput: unknown,
  risk: Risk,
  cut: boolean,
  cwd: string | undefined,
  permissionSuggestions: unknown,
  enabled = true,
): SessionGrant | null {
  if (!enabled || toolName !== "Bash" || risk === "high" || cut) return null;
  const command = isObj(toolInput) && typeof toolInput.command === "string" ? toolInput.command.trim() : null;
  if (command === null) return null;
  const ruleContent = suggestedRule(permissionSuggestions);
  if (ruleContent === null || !PRINTABLE_ASCII.test(ruleContent)) return null;

  let rule = ruleContent;
  let match: GrantMatch = "exact";
  if (ruleContent.endsWith(":*") || ruleContent.endsWith(" *")) {
    rule = ruleContent.slice(0, -2);
    match = "prefix";
  }
  if (rule.includes("*")) return null;

  if (match === "exact") {
    if (rule !== command) return null;
  } else {
    if (risk !== "low" || !isAllowlistedPrefix(rule)) return null;
    if (command !== rule && !command.startsWith(`${rule} `)) return null;
  }
  if (deniedProgram(rule)) return null;
  const bytes = utf8ByteLength(rule);
  if (bytes < GRANT_RULE_MIN_BYTES || bytes > LIMITS.grantRule) return null;
  if (!pathsInsideProject(rule, cwd)) return null;
  return { rule, match, ruleContent };
}
