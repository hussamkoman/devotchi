// Questions and plans of protocol 14.4 and 14.5: eligibility, frames, the
// plan summariser's `cut` and overflow rules, and the answer strings.

import { describe, expect, it } from "vitest";
import { MAX_FRAME_BYTES } from "../src/protocol.js";
import {
  ANSWER_NONE_OF_THESE,
  ANSWER_NO_PREFERENCE,
  answerStrings,
  buildPlanFrame,
  buildQuestionFrames,
  checkAnswers,
  questionSpecs,
  summarisePlan,
  type RequestFrameBase,
} from "../src/requests.js";
import { toDeviceText, utf8ByteLength } from "../src/text.js";

const BASE: RequestFrameBase = {
  id: "0123456789abcdef0123456789abcdef",
  sid: "3fa9c1",
  project: "devotchi",
  timeout: 30000,
  timestamp: "2026-11-15T09:00:00.000Z",
};

function question(fields: Record<string, unknown> = {}) {
  return {
    question: "Which framework should the dashboard use?",
    header: "Framework",
    options: [
      { label: "React", description: "Component library" },
      { label: "Vue", description: "Progressive framework" },
    ],
    multiSelect: false,
    ...fields,
  };
}

describe("toDeviceText", () => {
  it("keeps the device charset and maps line breaks and tabs to one space each", () => {
    expect(toDeviceText("a\nb\r\nc\td – “é” ✓")).toEqual({ text: "a b  c d – “é” ✓", replaced: 0, chars: 16 });
  });
  it("keeps \\n for permission.cmd", () => {
    expect(toDeviceText("a\nb", { keepNewlines: true }).text).toBe("a\nb");
  });
  it("decomposes letters outside the charset and replaces the rest with ?", () => {
    expect(toDeviceText("Łódź ﬁle 你好")).toEqual({ text: "?ódz file ??", replaced: 3, chars: 11 });
  });
  it("composes a letter and its combining accent first", () => {
    expect(toDeviceText("é").text).toBe("é");
  });
});

describe("questionSpecs and buildQuestionFrames", () => {
  it("frames the example of protocol 14.4", () => {
    const specs = questionSpecs({ questions: [question()] })!;
    expect(buildQuestionFrames(specs, BASE)).toEqual([
      {
        type: "question",
        id: BASE.id,
        index: 0,
        count: 1,
        header: "Framework",
        text: "Which framework should the dashboard use?",
        multi: false,
        options: [
          { label: "React", desc: "Component library" },
          { label: "Vue", desc: "Progressive framework" },
        ],
        sid: "3fa9c1",
        project: "devotchi",
        timeout: 30000,
        timestamp: "2026-11-15T09:00:00.000Z",
      },
    ]);
  });

  it("is eligible without a header, with an empty or non-string one, and shortens a 60-byte header", () => {
    for (const header of [undefined, "", 42]) {
      const [frame] = buildQuestionFrames(questionSpecs({ questions: [question({ header })] })!, BASE)!;
      expect(frame).not.toHaveProperty("header");
    }
    const [frame] = buildQuestionFrames(questionSpecs({ questions: [question({ header: "h".repeat(60) })] })!, BASE)!;
    expect(frame!.header).toBe(`${"h".repeat(21)}…`);
    expect(utf8ByteLength(frame!.header!)).toBe(24);
  });

  it("maps a line feed in the text to a space, not ?", () => {
    const [frame] = buildQuestionFrames(questionSpecs({ questions: [question({ question: "First\nsecond?" })] })!, BASE)!;
    expect(frame!.text).toBe("First second?");
  });

  it("sends one frame per question with index and count", () => {
    const questions = [0, 1, 2, 3].map((i) => question({ question: `Question ${i}?` }));
    const frames = buildQuestionFrames(questionSpecs({ questions })!, BASE)!;
    expect(frames.map((f) => [f.index, f.count, f.text])).toEqual([0, 1, 2, 3].map((i) => [i, 4, `Question ${i}?`]));
  });

  const ineligible: [string, unknown][] = [
    ["no questions array", {}],
    ["zero questions", { questions: [] }],
    ["5 questions", { questions: [0, 1, 2, 3, 4].map((i) => question({ question: `Q${i}?` })) }],
    ["one option", { questions: [question({ options: [{ label: "A" }] })] }],
    ["5 options", { questions: [question({ options: ["A", "B", "C", "D", "E"].map((label) => ({ label })) })] }],
    ["duplicate labels", { questions: [question({ options: [{ label: "A" }, { label: "A" }] })] }],
    ["labels equal after mapping", { questions: [question({ options: [{ label: "a\tb" }, { label: "a b" }] })] }],
    ["duplicate questions", { questions: [question(), question()] }],
    ["500-byte question", { questions: [question({ question: "q".repeat(500) })] }],
    ["81-byte label", { questions: [question({ options: [{ label: "l".repeat(81) }, { label: "B" }] })] }],
    ["empty label", { questions: [question({ options: [{ label: " " }, { label: "B" }] })] }],
    ["CJK labels", { questions: [question({ options: [{ label: "反应" }, { label: "视图" }] })] }],
    ["CJK question", { questions: [question({ question: "你想用哪个框架" })] }],
    ["non-boolean multiSelect", { questions: [question({ multiSelect: "yes" })] }],
    ["empty question", { questions: [question({ question: "" })] }],
  ];
  for (const [name, input] of ineligible) {
    it(`is not eligible: ${name}`, () => expect(questionSpecs(input)).toBeNull());
  }

  it("accepts a label with a fifth of its characters replaced, not more", () => {
    expect(questionSpecs({ questions: [question({ options: [{ label: "abcd你" }, { label: "B" }] })] })).not.toBeNull();
    expect(questionSpecs({ questions: [question({ options: [{ label: "abc你" }, { label: "B" }] })] })).toBeNull();
  });

  it("shortens option descriptions to keep a frame within 2048 bytes", () => {
    const options = [0, 1, 2, 3].map((i) => ({ label: `"${"\\".repeat(70)}${i}`, description: '"'.repeat(400) }));
    const specs = questionSpecs({ questions: [question({ question: '"'.repeat(480), options })] })!;
    const [frame] = buildQuestionFrames(specs, BASE)!;
    expect(utf8ByteLength(JSON.stringify({ ...frame, timeout: 300000 }))).toBeLessThanOrEqual(MAX_FRAME_BYTES);
    expect(frame!.text).toBe('"'.repeat(480));
    for (const option of frame!.options) expect(option.desc === undefined || option.desc.endsWith("…")).toBe(true);
  });
});

describe("answers", () => {
  const input = {
    questions: [
      question(),
      question({
        question: "Which sections?",
        multiSelect: true,
        options: [{ label: "Intro" }, { label: "Body" }, { label: "End" }],
      }),
    ],
  };
  const specs = questionSpecs(input)!;

  it("checks count, single choice, distinct and range", () => {
    expect(checkAnswers(specs, [[0], [0, 2]])).toBeNull();
    expect(checkAnswers(specs, [null, []])).toBeNull();
    expect(checkAnswers(specs, [[0]])).toMatch(/2 entries/);
    expect(checkAnswers(specs, [[0, 1], [0]])).toMatch(/exactly one/);
    expect(checkAnswers(specs, [[], [0]])).toMatch(/exactly one/);
    expect(checkAnswers(specs, [[0], [1, 1]])).toMatch(/twice/);
    expect(checkAnswers(specs, [[2], [0]])).toMatch(/does not have/);
    expect(checkAnswers(specs, [[0], [3]])).toMatch(/does not have/);
  });

  it("builds the answer strings from the original labels in option order", () => {
    expect(answerStrings(specs, [[1], [2, 0]])).toEqual({
      "Which framework should the dashboard use?": "Vue",
      "Which sections?": "Intro, End",
    });
    expect(answerStrings(specs, [null, []])).toEqual({
      "Which framework should the dashboard use?": ANSWER_NO_PREFERENCE,
      "Which sections?": ANSWER_NONE_OF_THESE,
    });
  });
});

describe("summarisePlan", () => {
  const rows: { name: string; plan: string; title: string; steps: string[]; more: number; cut: boolean }[] = [
    {
      name: "a flat list",
      plan: "# Add pet packs\n\n- Define the **pack** format\n- Add the `pets` partition\n1. Port [Devo](http://x)\n",
      title: "Add pet packs",
      steps: ["Define the pack format", "Add the pets partition", "Port Devo"],
      more: 0,
      cut: false,
    },
    {
      name: "a list and one prose line",
      plan: "# Plan\n\nWe will do this carefully.\n\n- One\n- Two",
      title: "Plan",
      steps: ["One", "Two"],
      more: 0,
      cut: true,
    },
    {
      name: "a nested list",
      plan: "# Plan\n- One\n  - One a\n- Two",
      title: "Plan",
      steps: ["One", "Two"],
      more: 0,
      cut: true,
    },
    {
      name: "a code block",
      plan: "# Plan\n- One\n```sh\n- not a step\n```\n- Two",
      title: "Plan",
      steps: ["One", "Two"],
      more: 0,
      cut: true,
    },
    {
      name: "a second heading",
      plan: "# Plan\n- One\n## Testing\n- Two",
      title: "Plan",
      steps: ["One", "Two"],
      more: 0,
      cut: true,
    },
    {
      name: "a table",
      plan: "# Plan\n- One\n| a | b |\n|---|---|",
      title: "Plan",
      steps: ["One"],
      more: 0,
      cut: true,
    },
    {
      name: "no list at all",
      plan: "Refactor the store\n\nSplit the reducer.\nAdd tests.\n",
      title: "Refactor the store",
      steps: ["Split the reducer.", "Add tests."],
      more: 0,
      cut: false,
    },
    {
      name: "no list, but a heading after the title",
      plan: "# Refactor\nSplit it.\n## Later\nAdd tests.",
      title: "Refactor",
      steps: ["Split it.", "Add tests."],
      more: 0,
      cut: true,
    },
    {
      name: "a character the charset cannot show",
      plan: "# Plan\n- Ship 🚀",
      title: "Plan",
      steps: ["Ship ?"],
      more: 0,
      cut: true,
    },
    {
      name: "a list item as the first line",
      plan: "- One\n- Two",
      title: "One",
      steps: ["Two"],
      more: 0,
      cut: false,
    },
  ];
  for (const row of rows) {
    it(row.name, () => {
      expect(summarisePlan(row.plan)).toEqual({ title: row.title, steps: row.steps, more: row.more, cut: row.cut });
    });
  }

  it("13 steps: 12 shown, more 1, cut", () => {
    const plan = `# Plan\n${Array.from({ length: 13 }, (_, i) => `- Step ${i + 1}`).join("\n")}`;
    const summary = summarisePlan(plan);
    expect(summary.steps).toHaveLength(12);
    expect(summary.steps[11]).toBe("Step 12");
    expect([summary.more, summary.cut]).toEqual([1, true]);
  });

  it("a 121-byte step is shortened with … and cut", () => {
    const summary = summarisePlan(`# Plan\n- ${"s".repeat(121)}`);
    expect(summary.steps[0]).toBe(`${"s".repeat(117)}…`);
    expect(utf8ByteLength(summary.steps[0]!)).toBe(120);
    expect(summary.cut).toBe(true);
  });

  it("a 120-byte step is whole", () => {
    expect(summarisePlan(`# Plan\n- ${"s".repeat(120)}`)).toMatchObject({ steps: ["s".repeat(120)], cut: false });
  });

  it("an 81-byte title is shortened with … and cut", () => {
    const summary = summarisePlan(`# ${"t".repeat(81)}\n- One`);
    expect(summary.title).toBe(`${"t".repeat(77)}…`);
    expect(summary.cut).toBe(true);
  });
});

describe("buildPlanFrame", () => {
  it("has the shape of protocol 14.5", () => {
    const frame = buildPlanFrame(summarisePlan("# Add pet packs\n- Define the pack format\n- Port Devo\n\nNotes."), BASE);
    expect(frame).toEqual({
      type: "plan",
      id: BASE.id,
      title: "Add pet packs",
      steps: ["Define the pack format", "Port Devo"],
      more: 0,
      cut: true,
      sid: "3fa9c1",
      project: "devotchi",
      timeout: 30000,
      timestamp: "2026-11-15T09:00:00.000Z",
    });
    expect(buildPlanFrame(summarisePlan("# T\n- a"), BASE)).not.toHaveProperty("cut");
  });

  it('drops steps of a plan made of " and \\ until the frame fits, and marks it cut', () => {
    const steps = Array.from({ length: 12 }, (_, i) => `- ${(i % 2 === 0 ? '"' : "\\").repeat(120)}`);
    const summary = summarisePlan(`# Plan\n${steps.join("\n")}`);
    expect(summary).toMatchObject({ more: 0, cut: false });
    const frame = buildPlanFrame(summary, BASE);
    expect(utf8ByteLength(JSON.stringify({ ...frame, timeout: 300000 }))).toBeLessThanOrEqual(MAX_FRAME_BYTES);
    expect(frame.steps.length).toBeLessThan(12);
    expect(frame.more).toBe(12 - frame.steps.length);
    expect(frame.cut).toBe(true);
  });
});
