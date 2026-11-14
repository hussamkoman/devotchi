import { describe, expect, it } from "vitest";
import { LIMITS, MAX_FRAME_BYTES } from "../src/protocol.js";
import { utf8ByteLength } from "../src/text.js";
import { RISK_RULES, formatToolCall, isOwnMcpTool, type FormattedToolCall } from "../src/tool-format.js";
import { buildPermission, serialize } from "../src/validate.js";

const CWD = "/home/u/proj";
const ID = "0123456789abcdef0123456789abcdef";

interface Case {
  name: string;
  tool: unknown;
  input: unknown;
  cwd?: string;
  expect: Partial<FormattedToolCall> & { detailsAbsent?: true };
}

const cases: Case[] = [
  // Bash
  {
    name: "small Bash (audit case)",
    tool: "Bash",
    input: { command: "ls -la", description: "List files" },
    expect: { tool: "Bash", summary: "ls -la", details: "List files\n$ ls -la", risk: "low" },
  },
  { name: "Bash without description", tool: "Bash", input: { command: "pnpm test" }, expect: { summary: "pnpm test", details: "$ pnpm test" } },
  {
    name: "Bash multi-line summary is the first line",
    tool: "Bash",
    input: { command: "\n  echo one\necho two" },
    expect: { summary: "echo one", details: "$ \n  echo one\necho two" },
  },
  { name: "Bash empty command", tool: "Bash", input: {}, expect: { summary: "(empty command)", details: "$ ", risk: "low" } },
  { name: "Bash tabs become spaces", tool: "Bash", input: { command: "a\tb" }, expect: { summary: "a  b" } },
  { name: "Bash non-ASCII is sanitised", tool: "Bash", input: { command: "echo héllo 😀" }, expect: { summary: "echo h?llo ?" } },
  { name: "Bash non-string command", tool: "Bash", input: { command: 42 }, expect: { summary: "(empty command)" } },
  { name: "Bash rm -rf is high", tool: "Bash", input: { command: "rm -rf dist" }, expect: { risk: "high" } },
  { name: "Bash git push is med", tool: "Bash", input: { command: "git push origin main" }, expect: { risk: "med" } },
  // Edit
  {
    name: "Edit inside cwd shows a relative path",
    tool: "Edit",
    input: { file_path: "/home/u/proj/src/app.c", old_string: "a\nb", new_string: "c" },
    cwd: CWD,
    expect: { tool: "Edit", summary: "Edit src/app.c", details: "-2 +1 lines\n- a\n- b\n+ c", risk: "low" },
  },
  {
    name: "Edit outside cwd is absolute and med",
    tool: "Edit",
    input: { file_path: "/tmp/x.txt", old_string: "", new_string: "y" },
    cwd: CWD,
    expect: { summary: "Edit /tmp/x.txt", details: "-0 +1 lines\n+ y", risk: "med" },
  },
  {
    name: "Edit escaping cwd with .. is outside",
    tool: "Edit",
    input: { file_path: "/home/u/proj/../other/f", old_string: "a", new_string: "b" },
    cwd: CWD,
    expect: { summary: "Edit /home/u/other/f", risk: "med" },
  },
  {
    name: "Edit with a relative path resolves against cwd",
    tool: "Edit",
    input: { file_path: "src/./x.ts", old_string: "a", new_string: "b" },
    cwd: CWD,
    expect: { summary: "Edit src/x.ts", risk: "low" },
  },
  {
    name: "Edit of a sibling directory with a common prefix is outside",
    tool: "Edit",
    input: { file_path: "/home/u/project2/a", old_string: "a", new_string: "b" },
    cwd: CWD,
    expect: { summary: "Edit /home/u/project2/a", risk: "med" },
  },
  {
    name: "Edit of .env is med",
    tool: "Edit",
    input: { file_path: "/home/u/proj/.env.local", old_string: "A=1", new_string: "A=2" },
    cwd: CWD,
    expect: { summary: "Edit .env.local", risk: "med" },
  },
  {
    name: "Edit of a git hook is high",
    tool: "Edit",
    input: { file_path: "/home/u/proj/.git/hooks/pre-commit", old_string: "", new_string: "x" },
    cwd: CWD,
    expect: { risk: "high" },
  },
  {
    name: "Edit with Windows separators",
    tool: "Edit",
    input: { file_path: "C:\\work\\proj\\src\\a.ts", old_string: "a", new_string: "b" },
    cwd: "C:\\work\\proj",
    expect: { summary: "Edit src/a.ts", risk: "low" },
  },
  {
    name: "Edit without a path",
    tool: "Edit",
    input: { old_string: "a", new_string: "b" },
    cwd: CWD,
    expect: { summary: "Edit (no path)", risk: "med" },
  },
  {
    name: "Edit with a long path keeps the tail",
    tool: "Edit",
    input: { file_path: `/home/u/proj/${"d/".repeat(40)}file.ts`, old_string: "a", new_string: "b" },
    cwd: CWD,
    expect: { summary: `Edit ...${`${"d/".repeat(40)}file.ts`.slice(-57)}` },
  },
  // MultiEdit
  {
    name: "MultiEdit counts edits",
    tool: "MultiEdit",
    input: {
      file_path: "/home/u/proj/a.ts",
      edits: [
        { old_string: "x", new_string: "y\nz" },
        { old_string: "p\nq", new_string: "" },
      ],
    },
    cwd: CWD,
    expect: { tool: "MultiEdit", summary: "Edit a.ts (2 edits)", details: "-3 +2 lines\n- x\n+ y\n+ z", risk: "low" },
  },
  {
    name: "MultiEdit with one edit",
    tool: "MultiEdit",
    input: { file_path: "/home/u/proj/a.ts", edits: [{ old_string: "x", new_string: "y" }] },
    cwd: CWD,
    expect: { summary: "Edit a.ts (1 edit)" },
  },
  { name: "MultiEdit with bad edits", tool: "MultiEdit", input: { file_path: "a.ts", edits: "nope" }, cwd: CWD, expect: { summary: "Edit a.ts (0 edits)" } },
  // Write
  {
    name: "Write counts lines and previews 8",
    tool: "Write",
    input: { file_path: "/home/u/proj/n.txt", content: Array.from({ length: 12 }, (_, i) => `line ${i + 1}`).join("\n") + "\n" },
    cwd: CWD,
    expect: {
      tool: "Write",
      summary: "Write n.txt (12 lines)",
      details: Array.from({ length: 8 }, (_, i) => `line ${i + 1}`).join("\n"),
      risk: "low",
    },
  },
  { name: "Write one line", tool: "Write", input: { file_path: "a", content: "x" }, cwd: CWD, expect: { summary: "Write a (1 line)", details: "x" } },
  { name: "Write empty file", tool: "Write", input: { file_path: "a", content: "" }, cwd: CWD, expect: { summary: "Write a (0 lines)", detailsAbsent: true } },
  { name: "Write CRLF content", tool: "Write", input: { file_path: "a", content: "x\r\ny\r\n" }, cwd: CWD, expect: { summary: "Write a (2 lines)", details: "x\ny" } },
  { name: "Write to /etc is high", tool: "Write", input: { file_path: "/etc/hosts", content: "x" }, cwd: CWD, expect: { risk: "high" } },
  { name: "Write to ~/.ssh is high", tool: "Write", input: { file_path: "/home/u/.ssh/authorized_keys", content: "k" }, cwd: CWD, expect: { risk: "high" } },
  { name: "Write to a tilde .ssh path is high", tool: "Write", input: { file_path: "~/.ssh/config", content: "k" }, cwd: CWD, expect: { risk: "high" } },
  { name: "Write absolute path without cwd is med", tool: "Write", input: { file_path: "/home/u/proj/a", content: "x" }, expect: { summary: "Write /home/u/proj/a (1 line)", risk: "med" } },
  { name: "Write relative path without cwd is low", tool: "Write", input: { file_path: "src/a", content: "x" }, expect: { summary: "Write src/a (1 line)", risk: "low" } },
  // Read, NotebookEdit, web, agents
  { name: "Read", tool: "Read", input: { file_path: "/home/u/proj/README.md" }, cwd: CWD, expect: { tool: "Read", summary: "Read README.md", risk: "low", detailsAbsent: true } },
  { name: "Read the cwd itself", tool: "Read", input: { file_path: "/home/u/proj" }, cwd: CWD, expect: { summary: "Read ." } },
  {
    name: "NotebookEdit",
    tool: "NotebookEdit",
    input: { notebook_path: "/home/u/proj/nb.ipynb", new_source: "print(1)" },
    cwd: CWD,
    expect: { tool: "NotebookEdit", summary: "Edit notebook nb.ipynb", details: "print(1)", risk: "low" },
  },
  {
    name: "WebFetch",
    tool: "WebFetch",
    input: { url: "https://docs.example.com/a/b?q=1", prompt: "Summarise\nthe page" },
    expect: { tool: "WebFetch", summary: "Fetch docs.example.com", details: "https://docs.example.com/a/b?q=1\nSummarise", risk: "low" },
  },
  { name: "WebFetch bad url", tool: "WebFetch", input: { url: "not a url" }, expect: { summary: "Fetch not a url", details: "not a url" } },
  { name: "WebSearch", tool: "WebSearch", input: { query: "esp-idf lcd" }, expect: { tool: "WebSearch", summary: "Search: esp-idf lcd", risk: "low" } },
  {
    name: "Task",
    tool: "Task",
    input: { subagent_type: "general-purpose", description: "Find bugs", prompt: "Look for bugs" },
    expect: { tool: "Task", summary: "Agent general-purpose: Find bugs", details: "Look for bugs", risk: "low" },
  },
  { name: "Agent without type", tool: "Agent", input: { description: "Explore" }, expect: { tool: "Agent", summary: "Agent: Explore" } },
  // MCP
  {
    name: "MCP tool with three of four arguments",
    tool: "mcp__github__create_issue",
    input: { repo: "o/r", title: "Bug", labels: ["a", "b"], body: "long" },
    expect: { tool: "github: create_issue", summary: 'repo="o/r" title="Bug" labels=["a","b"]', risk: "med" },
  },
  {
    name: "MCP argument values are cut to 40 bytes",
    tool: "mcp__srv__t",
    input: { text: "x".repeat(100) },
    expect: { summary: `text="${"x".repeat(39)}` },
  },
  { name: "MCP tool without arguments", tool: "mcp__srv__ping", input: {}, expect: { tool: "srv: ping", summary: "srv: ping", detailsAbsent: true } },
  { name: "MCP name with underscores in the tool", tool: "mcp__fs__read_text_file", input: { path: "/a" }, expect: { tool: "fs: read_text_file" } },
  { name: "Devotchi's own MCP tool", tool: "mcp__devotchi__show_status", input: { msg: "hi" }, expect: { tool: "devotchi: show_status", risk: "med" } },
  // Other tools and odd input
  { name: "unknown tool shows compact JSON", tool: "Frobnicate", input: { a: 1, b: [true] }, expect: { tool: "Frobnicate", summary: '{"a":1,"b":[true]}', risk: "med" } },
  { name: "Glob is known and low", tool: "Glob", input: { pattern: "**/*.ts" }, expect: { summary: '{"pattern":"**/*.ts"}', risk: "low" } },
  { name: "non-string tool name", tool: 42, input: { a: 1 }, expect: { tool: "Unknown tool", summary: '{"a":1}', risk: "med" } },
  { name: "null tool name and input", tool: null, input: null, expect: { tool: "Unknown tool", summary: "null", risk: "med" } },
  { name: "array input", tool: "Bash", input: ["rm -rf /"], expect: { summary: "(empty command)" } },
  { name: "string input to unknown tool", tool: "X", input: "plain", expect: { summary: '"plain"' } },
  { name: "BigInt input never throws", tool: "X", input: { n: 10n }, expect: { tool: "X", risk: "med" } },
  { name: "long tool name is cut to 40 bytes", tool: "T".repeat(80), input: {}, expect: { tool: "T".repeat(LIMITS.tool) } },
];

describe("formatToolCall table", () => {
  it("has at least 40 cases", () => expect(cases.length).toBeGreaterThanOrEqual(40));

  for (const c of cases) {
    it(c.name, () => {
      const out = formatToolCall(c.tool, c.input, c.cwd);
      const { detailsAbsent, ...fields } = c.expect;
      expect(out).toMatchObject(fields);
      if (detailsAbsent) expect(out).not.toHaveProperty("details");
    });
  }
});

describe("formatToolCall limits", () => {
  function expectWithinLimits(out: FormattedToolCall): void {
    expect(utf8ByteLength(out.tool)).toBeLessThanOrEqual(LIMITS.tool);
    expect(utf8ByteLength(out.summary)).toBeLessThanOrEqual(LIMITS.summary);
    if (out.details !== undefined) expect(utf8ByteLength(out.details)).toBeLessThanOrEqual(LIMITS.details);
    for (const text of [out.tool, out.summary, out.details ?? ""]) expect(text).toMatch(/^[\x20-\x7e\n]*$/);
    expect(out.summary).not.toContain("\n");
    const frame = serialize(buildPermission({ id: ID, ...out, project: "proj", sid: "3fa9c1", timeout: 30000, timestamp: new Date().toISOString() }));
    expect(utf8ByteLength(frame)).toBeLessThanOrEqual(MAX_FRAME_BYTES);
  }

  it("long Edit (audit case) is cut to the limits with 6 + 6 preview lines", () => {
    const oldText = Array.from({ length: 200 }, (_, i) => `const old${i} = ${"x".repeat(60)};`).join("\n");
    const newText = Array.from({ length: 180 }, (_, i) => `const new${i} = ${"y".repeat(60)};`).join("\n");
    const out = formatToolCall("Edit", { file_path: "/home/u/proj/src/big.ts", old_string: oldText, new_string: newText }, CWD);
    expect(out.summary).toBe("Edit src/big.ts");
    expect(out.details!.startsWith("-200 +180 lines\n- const old0 = ")).toBe(true);
    expect(utf8ByteLength(out.details!)).toBe(LIMITS.details);
    expectWithinLimits(out);
  });

  it("CJK Write (audit case) becomes ASCII within the limits", () => {
    const content = Array.from({ length: 40 }, () => "中文内容".repeat(25)).join("\n");
    const out = formatToolCall("Write", { file_path: "/home/u/proj/文档/说明.md", content }, CWD);
    expect(out.summary).toBe("Write ??/??.md (40 lines)");
    expect(out.details!.startsWith("????")).toBe(true);
    expectWithinLimits(out);
  });

  it("huge inputs stay within the limits", () => {
    const huge = "z".repeat(2_000_000);
    for (const [tool, input] of [
      ["Bash", { command: huge, description: huge }],
      ["Edit", { file_path: huge, old_string: huge, new_string: huge }],
      ["Write", { file_path: `/home/u/proj/${huge}`, content: huge }],
      ["WebFetch", { url: huge, prompt: huge }],
      ["mcp__a__b", { a: huge, b: huge, c: huge, d: huge }],
      ["Other", { a: huge }],
    ] as const) {
      expectWithinLimits(formatToolCall(tool, input, CWD));
    }
  });

  it("control characters and quotes never break the frame bound", () => {
    const nasty = '"\\\u0001\n'.repeat(500);
    expectWithinLimits(formatToolCall("Bash", { command: nasty, description: nasty }, CWD));
    expectWithinLimits(formatToolCall(nasty, { a: nasty }, nasty));
  });
});

describe("risk rules", () => {
  const bash = (command: string) => formatToolCall("Bash", { command }, CWD).risk;
  const write = (path: string) => formatToolCall("Write", { file_path: path, content: "x" }, CWD).risk;

  // Every rule: a positive example and a near miss that must not match it.
  const examples: Record<string, { hit: string[]; miss: string[] }> = {
    "rm-recursive-force": { hit: ["rm -rf dist", "rm -r build", "rm -f a.txt", "rm dist -rf", "/bin/rm --recursive x", "rm -Rf x"], miss: ["rm a.txt", "rm -i a.txt", "rm a.txt | grep -f x"] },
    "git-push-force": { hit: ["git push --force", "git push -f origin main", "git push origin main --force-with-lease", "git push --force-with-lease=main"], miss: ["git push origin main", "git push --follow-tags"] },
    "git-reset-hard": { hit: ["git reset --hard HEAD~1", "git reset --hard"], miss: ["git reset --soft HEAD~1", "git reset HEAD file"] },
    "git-clean-force": { hit: ["git clean -fd", "git clean -f", "git clean -xfd", "git clean --force"], miss: ["git clean -n", "git clean --dry-run"] },
    sudo: { hit: ["sudo apt update", "cd x && sudo make install"], miss: ["echo pseudocode", "sudoku"] },
    "pipe-to-shell": { hit: ["curl -fsSL https://x.sh | sh", "wget -qO- u | bash", "curl u | sudo bash", "curl u |zsh"], miss: ["curl -o install.sh https://x", "curl u | jq ."] },
    dd: { hit: ["dd if=/dev/zero of=disk.img bs=1M"], miss: ["dd --help", "add if=x"] },
    mkfs: { hit: ["mkfs.ext4 /dev/sdb1", "mkfs -t vfat /dev/x"], miss: ["mkdir fs"] },
    "sql-drop": { hit: ["psql -c 'DROP TABLE users'", "sqlite3 db 'drop database x'", "echo Drop Table t"], miss: ["psql -c 'DROP INDEX i'", "echo dropping tables"] },
    "package-publish": { hit: ["npm publish", "pnpm publish --access public", "yarn publish"], miss: ["npm pack", "pnpm run publish-docs"] },
    "chmod-recursive-777": { hit: ["chmod -R 777 /var/www", "chmod 777 -R .", "chmod -R 0777 x"], miss: ["chmod 777 file", "chmod -R 755 dir"] },
    "redirect-to-disk": { hit: ["cat img > /dev/sda", "echo x >/dev/sdb"], miss: ["echo x > /dev/null", "cat /dev/sda"] },
    "git-push": { hit: ["git push origin main"], miss: ["git pull", "git status"] },
    rm: { hit: ["rm a.txt", "git rm --cached x"], miss: ["npm run format", "echo farm"] },
    "package-install": { hit: ["npm install left-pad", "pnpm add -D vitest", "yarn add x", "pip install requests", "pip3 install x", "cargo add serde", "apt-get install x", "brew install jq", "npm i x"], miss: ["npm run build", "pnpm test"] },
    docker: { hit: ["docker run --rm alpine", "docker compose up"], miss: ["cat Dockerfile.txt"] },
    network: { hit: ["curl https://x", "wget u", "ssh host", "scp a host:b", "nc -l 9000"], miss: ["echo curling", "cat sshd_config"] },
    kill: { hit: ["kill 1234", "pkill node", "killall x"], miss: ["echo skill"] },
    "chmod-chown": { hit: ["chmod +x a.sh", "chown u:g f"], miss: ["echo chmodding"] },
    "write-ssh": { hit: ["/home/u/.ssh/authorized_keys", "~/.ssh/config"], miss: ["/home/u/proj/ssh/config", "/home/u/proj/.sshrc"] },
    "write-etc": { hit: ["/etc/hosts", "/etc/nginx/nginx.conf"], miss: ["/home/u/proj/etc/hosts"] },
    "write-git-hooks": { hit: ["/home/u/proj/.git/hooks/pre-commit", ".git/hooks/post-merge"], miss: ["/home/u/proj/.github/hooks/x", "/home/u/proj/git/hooks/x"] },
    "write-env": { hit: ["/home/u/proj/.env", "/home/u/proj/config/.env.production"], miss: ["/home/u/proj/env.ts", "/home/u/proj/.envrc.d/x"] },
  };

  it("has examples for every rule", () => {
    expect(Object.keys(examples).sort()).toEqual(RISK_RULES.map((rule) => rule.id).sort());
  });

  for (const rule of RISK_RULES) {
    describe(`${rule.id} (${rule.risk})`, () => {
      for (const text of examples[rule.id]?.hit ?? []) {
        it(`matches ${JSON.stringify(text)}`, () => {
          expect(rule.pattern.test(text)).toBe(true);
          const risk = rule.target === "bash" ? bash(text) : write(text);
          if (rule.risk === "high") expect(risk).toBe("high");
          else expect(risk).not.toBe("low");
        });
      }
      for (const text of examples[rule.id]?.miss ?? []) {
        it(`does not match ${JSON.stringify(text)}`, () => expect(rule.pattern.test(text)).toBe(false));
      }
    });
  }

  it("rates plain commands and in-tree writes low", () => {
    expect(bash("pnpm test")).toBe("low");
    expect(bash("ls -la && cat README.md")).toBe("low");
    expect(write("/home/u/proj/src/a.ts")).toBe("low");
  });

  it("rates med rules med", () => {
    expect(bash("rm a.txt")).toBe("med");
    expect(bash("git push")).toBe("med");
    expect(write("/home/u/proj/.env")).toBe("med");
  });

  it("rates every MCP tool and every unknown tool med", () => {
    expect(formatToolCall("mcp__x__y", {}).risk).toBe("med");
    expect(formatToolCall("SomethingNew", {}).risk).toBe("med");
  });
});

describe("isOwnMcpTool", () => {
  it.each([
    ["mcp__devotchi__show_status", true],
    ["mcp__devotchi__set_mood", true],
    ["mcp__devotchix__a", false],
    ["mcp__other__devotchi", false],
    ["Bash", false],
    [42, false],
  ])("%j -> %s", (name, own) => expect(isOwnMcpTool(name)).toBe(own));
});
