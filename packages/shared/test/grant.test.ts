// Protocol 14.2 rules 1-8: one row per case.

import { describe, expect, it } from "vitest";
import { GRANT_DENIED_FIRST_WORDS, SESSION_PREFIX_FAMILIES, deriveGrant } from "../src/grant.js";
import type { Risk } from "../src/protocol.js";

const CWD = "/home/dev/project";

function suggestion(ruleContent: unknown, extra: Record<string, unknown> = {}): unknown[] {
  return [
    {
      type: "addRules",
      rules: [{ toolName: "Bash", ruleContent }],
      behavior: "allow",
      destination: "localSettings",
      ...extra,
    },
  ];
}

interface Row {
  name: string;
  command: string;
  rule?: unknown;
  suggestions?: unknown;
  risk?: Risk;
  cut?: boolean;
  tool?: string;
  enabled?: boolean;
}

function grantFor(row: Row) {
  return deriveGrant(
    row.tool ?? "Bash",
    { command: row.command, description: "x" },
    row.risk ?? "low",
    row.cut ?? false,
    CWD,
    row.suggestions ?? suggestion(row.rule),
    row.enabled ?? true,
  );
}

describe("deriveGrant: granted", () => {
  const rows: (Row & { expected: { rule: string; match: "exact" | "prefix" } })[] = [
    { name: "exact, low risk", command: "pnpm test", rule: "pnpm test", expected: { rule: "pnpm test", match: "exact" } },
    {
      name: "exact, med risk",
      command: "pnpm test",
      rule: "pnpm test",
      risk: "med",
      expected: { rule: "pnpm test", match: "exact" },
    },
    { name: "pnpm test:*", command: "pnpm test --run", rule: "pnpm test:*", expected: { rule: "pnpm test", match: "prefix" } },
    { name: "pnpm test *", command: "pnpm test", rule: "pnpm test *", expected: { rule: "pnpm test", match: "prefix" } },
    {
      name: "npm run build:*",
      command: "npm run build -- --watch",
      rule: "npm run build:*",
      expected: { rule: "npm run build", match: "prefix" },
    },
    {
      name: "yarn run test:unit:*",
      command: "yarn run test:unit",
      rule: "yarn run test:unit:*",
      expected: { rule: "yarn run test:unit", match: "prefix" },
    },
    { name: "cargo check:*", command: "cargo check --all", rule: "cargo check:*", expected: { rule: "cargo check", match: "prefix" } },
    { name: "go vet:*", command: "go vet ./...", rule: "go vet:*", expected: { rule: "go vet", match: "prefix" } },
    { name: "git status:*", command: "git status --short", rule: "git status:*", expected: { rule: "git status", match: "prefix" } },
    { name: "bun test:*", command: "bun test", rule: "bun test:*", expected: { rule: "bun test", match: "prefix" } },
    {
      name: "a path inside the project",
      command: `cat ${CWD}/README.md`,
      rule: `cat ${CWD}/README.md`,
      expected: { rule: `cat ${CWD}/README.md`, match: "exact" },
    },
    { name: "a relative path", command: "cat ./README.md", rule: "cat ./README.md", expected: { rule: "cat ./README.md", match: "exact" } },
    {
      name: "suggestions of other types are ignored",
      command: "pnpm test",
      suggestions: [{ type: "setMode", mode: "acceptEdits", destination: "session" }, ...suggestion("pnpm test")],
      expected: { rule: "pnpm test", match: "exact" },
    },
  ];
  for (const row of rows) {
    it(row.name, () => {
      const last = (row.suggestions as { rules: { ruleContent: string }[] }[] | undefined)?.at(-1);
      const rule = last?.rules[0]?.ruleContent ?? row.rule;
      expect(grantFor(row)).toEqual({ ...row.expected, ruleContent: rule });
    });
  }
});

describe("deriveGrant: withheld, prefix not on the allowlist", () => {
  const rules = [
    "timeout 60:*",
    "nice -n:*",
    "pnpm exec:*",
    "pnpm dlx:*",
    "npm exec:*",
    "yarn dlx:*",
    "bun x:*",
    "uv run:*",
    "poetry run:*",
    "docker run:*",
    "docker exec:*",
    "kubectl exec:*",
    "find .:*",
    "git -c:*",
    "git commit:*",
    "make -C:*",
    "make test:*",
    "watch -n:*",
    "ls -la:*",
    "pnpm run:*",
    "pnpm test --:*",
  ];
  for (const rule of rules) {
    it(rule, () => {
      const prefix = rule.slice(0, -2);
      expect(grantFor({ name: rule, command: `${prefix} more args`, rule })).toBeNull();
    });
  }
});

describe("deriveGrant: withheld, denylist on an exact rule", () => {
  for (const word of GRANT_DENIED_FIRST_WORDS) {
    it(`first word ${word}`, () => {
      const command = `${word} script-name`;
      expect(grantFor({ name: word, command, rule: command })).toBeNull();
    });
  }
  const commands = [
    "/usr/bin/env FOO=1 x",
    "pnpm exec vitest",
    "uv run pytest",
    "docker run alpine",
    "git -c core.pager=x log",
    "git -C sub status",
    "git --config-env=core.pager=X log",
    "bun script.ts",
    "go run .",
    "cargo run --release",
    "FOO=1 node evil.js",
    "/usr/local/bin/python3 x.py",
  ];
  for (const command of commands) {
    it(command, () => expect(grantFor({ name: command, command, rule: command })).toBeNull());
  }
});

describe("deriveGrant: withheld, other rules", () => {
  const rows: Row[] = [
    { name: "wildcard inside", command: "pnpm test foo", rule: "pnpm * foo" },
    { name: "wildcard at the front", command: "pnpm test", rule: "* test" },
    { name: "one-word prefix", command: "pnpm test", rule: "pnpm:*" },
    { name: "allowlisted prefix with med risk", command: "pnpm test && curl x", rule: "pnpm test:*", risk: "med" },
    { name: "high risk", command: "pnpm test", rule: "pnpm test", risk: "high" },
    { name: "cut request", command: "pnpm test", rule: "pnpm test", cut: true },
    {
      name: "two addRules suggestions",
      command: "pnpm test",
      suggestions: [...suggestion("pnpm test"), ...suggestion("pnpm test:*")],
    },
    {
      name: "two rules in one entry",
      command: "pnpm test",
      suggestions: [
        {
          type: "addRules",
          rules: [
            { toolName: "Bash", ruleContent: "pnpm test" },
            { toolName: "Bash", ruleContent: "pnpm lint" },
          ],
          behavior: "allow",
          destination: "localSettings",
        },
      ],
    },
    { name: "wrong tool", command: "pnpm test", rule: "pnpm test", tool: "Edit" },
    {
      name: "rule for another tool",
      command: "pnpm test",
      suggestions: [{ type: "addRules", rules: [{ toolName: "Edit", ruleContent: "pnpm test" }], behavior: "allow" }],
    },
    { name: 'behavior: "deny"', command: "pnpm test", suggestions: suggestion("pnpm test", { behavior: "deny" }) },
    {
      name: "missing ruleContent",
      command: "pnpm test",
      suggestions: [{ type: "addRules", rules: [{ toolName: "Bash" }], behavior: "allow" }],
    },
    { name: "non-string ruleContent", command: "pnpm test", rule: 42 },
    { name: "non-ASCII", command: "echo héllo", rule: "echo héllo" },
    { name: "command not matching the rule", command: "pnpm lint", rule: "pnpm test" },
    { name: "prefix where the command continues without a space", command: "pnpm tests", rule: "pnpm test:*" },
    { name: "1-byte rule", command: "x", rule: "x" },
    { name: "121-byte rule", command: `echo ${"a".repeat(116)}`, rule: `echo ${"a".repeat(116)}` },
    { name: "switch off", command: "pnpm test", rule: "pnpm test", enabled: false },
    { name: "suggestions not an array", command: "pnpm test", suggestions: { type: "addRules" } },
  ];
  for (const row of rows) {
    it(row.name, () => expect(grantFor(row)).toBeNull());
  }
  it("no suggestions", () => {
    expect(deriveGrant("Bash", { command: "pnpm test" }, "low", false, CWD, undefined)).toBeNull();
  });
  it("a 120-byte rule is still granted", () => {
    const command = `echo ${"a".repeat(115)}`;
    expect(grantFor({ name: "120", command, rule: command })).toMatchObject({ rule: command });
  });
});

describe("deriveGrant: withheld, paths (rule 8)", () => {
  const commands = [
    "cat ../secret",
    "cat ~/.ssh/id_rsa",
    "cat /etc/passwd",
    "tool --out=/tmp/x",
    "cat sub/../../x",
    'cat "/etc/passwd"',
    "cat '../secret'",
    'tool --out="/tmp/x"',
  ];
  for (const command of commands) {
    it(command, () => expect(grantFor({ name: command, command, rule: command })).toBeNull());
  }
  it("an absolute path without a cwd", () => {
    expect(deriveGrant("Bash", { command: "cat /a/b" }, "low", false, undefined, suggestion("cat /a/b"))).toBeNull();
  });
});

describe("SESSION_PREFIX_FAMILIES", () => {
  it("holds no family whose words can name another program", () => {
    const words = SESSION_PREFIX_FAMILIES.flatMap((family) => [...family.words]);
    for (const banned of ["exec", "dlx", "x", "create", "-c", "-C", "-exec"]) expect(words).not.toContain(banned);
    for (const family of SESSION_PREFIX_FAMILIES) {
      for (const program of family.programs) expect(GRANT_DENIED_FIRST_WORDS).not.toContain(program);
    }
  });
});
