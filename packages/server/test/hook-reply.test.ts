// Golden tests of the hook reply builder (protocol 14.3, 14.4). Claude Code
// treats a reply that fails its schema as "no decision", so every reply is
// compared byte for byte with the JSON the protocol specifies.

import { describe, expect, it } from "vitest";
import { answerStrings, questionSpecs, type QuestionAnswer } from "@devotchi/shared";
import { hookReply } from "../src/routes/hooks.js";
import type { PermissionOutcome } from "../src/permissions/manager.js";

const wrap = (decision: string) => `{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":${decision}}}`;
const serialised = (outcome: PermissionOutcome) => JSON.stringify(hookReply(outcome));

const PLAN_INPUT = { plan: "# Add pet packs\n\n- Define the pack format", planFilePath: "/tmp/plan.md" };

describe("hookReply: permissions", () => {
  it("allow", () => {
    expect(serialised({ kind: "allow", by: "sim-a", answer: { choice: null } })).toBe(wrap('{"behavior":"allow"}'));
  });

  it("deny", () => {
    expect(serialised({ kind: "deny", by: "sim-a", choice: null })).toBe(
      wrap('{"behavior":"deny","message":"Denied on Devotchi. Do not retry this call; try another way or ask the user."}'),
    );
  });

  it("allow_session", () => {
    expect(serialised({ kind: "allow", by: "sim-a", answer: { choice: "allow_session", ruleContent: "pnpm test:*" } })).toBe(
      wrap(
        '{"behavior":"allow","updatedPermissions":[{"type":"addRules","rules":[{"toolName":"Bash","ruleContent":"pnpm test:*"}],"behavior":"allow","destination":"session"}]}',
      ),
    );
  });

  it("deny_ask_why", () => {
    expect(serialised({ kind: "deny", by: "sim-a", choice: "deny_ask_why" })).toBe(
      wrap(
        '{"behavior":"deny","message":"Denied on Devotchi for now. The user wants to know why: explain what this call is for and why it is needed, then you may ask again."}',
      ),
    );
  });

  it("deny_stop", () => {
    expect(serialised({ kind: "deny", by: "sim-a", choice: "deny_stop" })).toBe(
      wrap('{"behavior":"deny","message":"Denied on Devotchi. The user asked you to stop.","interrupt":true}'),
    );
  });
});

describe("hookReply: plans", () => {
  const input = JSON.stringify(PLAN_INPUT);

  it("approve", () => {
    expect(serialised({ kind: "allow", by: "sim-a", answer: { choice: "approve", toolInput: PLAN_INPUT } })).toBe(
      wrap(
        `{"behavior":"allow","updatedInput":${input},"updatedPermissions":[{"type":"setMode","mode":"default","destination":"session"}]}`,
      ),
    );
  });

  it("approve_accept_edits", () => {
    expect(serialised({ kind: "allow", by: "sim-a", answer: { choice: "approve_accept_edits", toolInput: PLAN_INPUT } })).toBe(
      wrap(
        `{"behavior":"allow","updatedInput":${input},"updatedPermissions":[{"type":"setMode","mode":"acceptEdits","destination":"session"}]}`,
      ),
    );
  });

  it("revise", () => {
    expect(serialised({ kind: "deny", by: "sim-a", choice: "revise" })).toBe(
      wrap(
        '{"behavior":"deny","message":"The user read the plan on Devotchi and wants changes. Stay in plan mode and ask the user what to change."}',
      ),
    );
  });

  it("deny_stop on a plan is the same deny_stop", () => {
    expect(serialised({ kind: "deny", by: "sim-a", choice: "deny_stop" })).toContain('"interrupt":true');
  });
});

describe("hookReply: questions", () => {
  const framework = {
    question: "Which framework should the dashboard use?",
    header: "Framework",
    options: [
      { label: "React", description: "Component library" },
      { label: "Vue", description: "Progressive framework" },
    ],
    multiSelect: false,
  };
  const sections = {
    question: "Which sections?",
    header: "Sections",
    options: [{ label: "Intro" }, { label: "Body" }, { label: "Conclusion" }],
    multiSelect: true,
  };

  function answered(toolInput: Record<string, unknown>, answers: QuestionAnswer[]): string {
    const specs = questionSpecs(toolInput)!;
    expect(specs).not.toBeNull();
    return serialised({
      kind: "allow",
      by: "sim-a",
      answer: { choice: "answered", toolInput, answers: answerStrings(specs, answers) },
    });
  }

  it("single choice", () => {
    const input = { questions: [framework] };
    expect(answered(input, [[1]])).toBe(
      wrap(`{"behavior":"allow","updatedInput":{"questions":${JSON.stringify([framework])},"answers":{"Which framework should the dashboard use?":"Vue"}}}`),
    );
  });

  it("multi-select joined with \", \" in option order", () => {
    const input = { questions: [sections] };
    expect(answered(input, [[2, 0]])).toBe(
      wrap(`{"behavior":"allow","updatedInput":{"questions":${JSON.stringify([sections])},"answers":{"Which sections?":"Intro, Conclusion"}}}`),
    );
  });

  it("an empty multi-select is \"None of these\"", () => {
    const input = { questions: [sections] };
    expect(answered(input, [[]])).toBe(
      wrap(`{"behavior":"allow","updatedInput":{"questions":${JSON.stringify([sections])},"answers":{"Which sections?":"None of these"}}}`),
    );
  });

  it("null is \"No preference, use your judgment\"", () => {
    const input = { questions: [framework] };
    expect(answered(input, [null])).toBe(
      wrap(
        `{"behavior":"allow","updatedInput":{"questions":${JSON.stringify([framework])},"answers":{"Which framework should the dashboard use?":"No preference, use your judgment"}}}`,
      ),
    );
  });

  it("four questions, every one gets a key", () => {
    const questions = [1, 2, 3, 4].map((n) => ({
      question: `Question ${n}?`,
      header: `H${n}`,
      options: [{ label: `A${n}` }, { label: `B${n}` }],
    }));
    const input = { questions };
    expect(answered(input, [[0], [1], null, [0]])).toBe(
      wrap(
        `{"behavior":"allow","updatedInput":{"questions":${JSON.stringify(questions)},"answers":{"Question 1?":"A1","Question 2?":"B2","Question 3?":"No preference, use your judgment","Question 4?":"A4"}}}`,
      ),
    );
  });

  it("keeps extra keys of tool_input and uses the original strings, not the device text", () => {
    const odd = {
      question: "Which\nicon? ✨",
      options: [{ label: "Stern ✨" }, { label: "Mond" }],
    };
    const input = { questions: [odd], metadata: { source: "x" }, preview: true };
    expect(answered(input, [[0]])).toBe(
      wrap(
        `{"behavior":"allow","updatedInput":{"questions":${JSON.stringify([odd])},"metadata":{"source":"x"},"preview":true,"answers":{"Which\\nicon? ✨":"Stern ✨"}}}`,
      ),
    );
  });

  it("overwrites an answers object that was already there", () => {
    const input = { questions: [framework], answers: { "Which framework should the dashboard use?": "Angular", other: "x" } };
    expect(answered(input, [[0]])).toBe(
      wrap(
        `{"behavior":"allow","updatedInput":{"questions":${JSON.stringify([framework])},"answers":{"Which framework should the dashboard use?":"React"}}}`,
      ),
    );
  });

  it("decline", () => {
    expect(serialised({ kind: "deny", by: "sim-a", choice: "decline" })).toBe(
      wrap('{"behavior":"deny","message":"The user declined to answer on Devotchi. Use your best judgment, or ask again later."}'),
    );
  });
});

describe("hookReply: neutral", () => {
  it.each(["timeout", "holder_lost", "no_ack", "deferred", "cancelled", "skipped_tool", "desk", "shutdown"] as const)(
    "%s is {}",
    (detail) => {
      expect(serialised({ kind: "neutral", detail })).toBe("{}");
    },
  );

  it("an aborted hook is {}", () => {
    expect(serialised({ kind: "none" })).toBe("{}");
  });
});
