// Questions (AskUserQuestion) and plans (ExitPlanMode) of protocol 14.4 and
// 14.5: eligibility, the frames consumers get, and the answer strings the
// hook reply is built from. Everything here is pure.

import {
  LIMITS,
  MAX_FRAME_BYTES,
  MAX_OPTIONS,
  MAX_QUESTIONS,
  MIN_OPTIONS,
  PLAN_MAX_STEPS,
  type PlanMessage,
  type QuestionAnswer,
  type QuestionMessage,
  type QuestionOption,
} from "./protocol.js";
import { shortenWithEllipsis, toDeviceText, utf8ByteLength } from "./text.js";

type Obj = Record<string, unknown>;

const isObj = (value: unknown): value is Obj => typeof value === "object" && value !== null && !Array.isArray(value);

/**
 * Size of a frame as sent at any later time: replays and `extend` change only
 * `timeout`, which never has more digits than this.
 */
function frameBytes(msg: object): number {
  return utf8ByteLength(JSON.stringify({ ...msg, timeout: 999_999 }));
}

/** Fields every request frame carries besides its content. */
export interface RequestFrameBase {
  id: string;
  sid?: string;
  project?: string;
  timeout: number;
  timestamp?: string;
}

function withBase<T extends object>(base: RequestFrameBase, content: T, type: string): T {
  const out: Obj = { type, id: base.id, ...content };
  if (base.sid !== undefined) out.sid = base.sid;
  if (base.project !== undefined) out.project = base.project;
  out.timeout = base.timeout;
  if (base.timestamp !== undefined) out.timestamp = base.timestamp;
  return out as T;
}

// --- Questions (14.4) ---

export interface QuestionSpec {
  /** The hook's own `question` string, the key of `answers`. */
  question: string;
  /** The hook's own option labels, the values of `answers`. */
  labels: string[];
  text: string;
  header?: string;
  multi: boolean;
  options: QuestionOption[];
}

/** Device text of a question or label, or null when it is not eligible. */
function eligibleText(value: unknown, maxBytes: number): string | null {
  if (typeof value !== "string") return null;
  const mapped = toDeviceText(value);
  const text = mapped.text.trim();
  if (text === "" || utf8ByteLength(text) > maxBytes) return null;
  // More than a fifth of the characters unreadable on the device.
  if (mapped.replaced * 5 > mapped.chars) return null;
  return text;
}

function optionalText(value: unknown, maxBytes: number): string | undefined {
  if (typeof value !== "string") return undefined;
  const text = shortenWithEllipsis(toDeviceText(value).text.trim(), maxBytes);
  return text === "" ? undefined : text;
}

function unique(values: string[]): boolean {
  return new Set(values).size === values.length;
}

/**
 * The questions of an AskUserQuestion `tool_input` when the call can be
 * answered on a consumer (protocol 14.4 "Eligibility"), otherwise null.
 */
export function questionSpecs(toolInput: unknown): QuestionSpec[] | null {
  if (!isObj(toolInput) || !Array.isArray(toolInput.questions)) return null;
  const questions = toolInput.questions as unknown[];
  if (questions.length < 1 || questions.length > MAX_QUESTIONS) return null;
  const specs: QuestionSpec[] = [];
  for (const q of questions) {
    if (!isObj(q)) return null;
    const text = eligibleText(q.question, LIMITS.questionText);
    if (text === null) return null;
    if (q.multiSelect !== undefined && typeof q.multiSelect !== "boolean") return null;
    if (!Array.isArray(q.options) || q.options.length < MIN_OPTIONS || q.options.length > MAX_OPTIONS) return null;
    const labels: string[] = [];
    const options: QuestionOption[] = [];
    for (const option of q.options as unknown[]) {
      if (!isObj(option)) return null;
      const label = eligibleText(option.label, LIMITS.label);
      if (label === null) return null;
      labels.push(option.label as string);
      const desc = optionalText(option.description, LIMITS.optionDesc);
      options.push(desc === undefined ? { label } : { label, desc });
    }
    if (!unique(labels) || !unique(options.map((o) => o.label))) return null;
    const spec: QuestionSpec = { question: q.question as string, labels, text, multi: q.multiSelect === true, options };
    const header = optionalText(q.header, LIMITS.header);
    if (header !== undefined) spec.header = header;
    specs.push(spec);
  }
  if (!unique(specs.map((s) => s.question)) || !unique(specs.map((s) => s.text))) return null;
  return specs;
}

function questionFrame(spec: QuestionSpec, index: number, count: number, base: RequestFrameBase, descBytes: number): QuestionMessage {
  const content: Obj = { index, count };
  if (spec.header !== undefined) content.header = spec.header;
  content.text = spec.text;
  content.multi = spec.multi;
  content.options = spec.options.map((option) => {
    const desc = option.desc === undefined ? "" : shortenWithEllipsis(option.desc, descBytes);
    return desc === "" ? { label: option.label } : { label: option.label, desc };
  });
  return withBase(base, content, "question") as unknown as QuestionMessage;
}

/**
 * One frame per question, sent back to back. Option descriptions are
 * shortened as far as needed to keep each frame within 2048 bytes; null when
 * a frame does not fit even without them.
 */
export function buildQuestionFrames(specs: readonly QuestionSpec[], base: RequestFrameBase): QuestionMessage[] | null {
  const frames: QuestionMessage[] = [];
  for (const [index, spec] of specs.entries()) {
    let frame = questionFrame(spec, index, specs.length, base, LIMITS.optionDesc);
    if (frameBytes(frame) > MAX_FRAME_BYTES) {
      // The largest common description budget that fits.
      let low = 0;
      let high = LIMITS.optionDesc;
      while (low < high) {
        const mid = Math.ceil((low + high) / 2);
        if (frameBytes(questionFrame(spec, index, specs.length, base, mid)) <= MAX_FRAME_BYTES) low = mid;
        else high = mid - 1;
      }
      frame = questionFrame(spec, index, specs.length, base, low);
      if (frameBytes(frame) > MAX_FRAME_BYTES) return null;
    }
    frames.push(frame);
  }
  return frames;
}

/**
 * Checks a `question_response.answers` array against the questions; returns
 * an error text, or null when it is valid (protocol 14.4 "Answer").
 */
export function checkAnswers(specs: readonly QuestionSpec[], answers: readonly QuestionAnswer[]): string | null {
  if (answers.length !== specs.length) return `answers must have ${specs.length} entries`;
  for (const [index, answer] of answers.entries()) {
    if (answer === null) continue;
    const spec = specs[index]!;
    if (!spec.multi && answer.length !== 1) return `answers[${index}] must name exactly one option`;
    if (new Set(answer).size !== answer.length) return `answers[${index}] names an option twice`;
    if (answer.some((option) => option < 0 || option >= spec.labels.length)) {
      return `answers[${index}] names an option the question does not have`;
    }
  }
  return null;
}

export const ANSWER_NONE_OF_THESE = "None of these";
export const ANSWER_NO_PREFERENCE = "No preference, use your judgment";

/** The `answers` object of the hook reply, from the original strings only. Expects checked answers. */
export function answerStrings(specs: readonly QuestionSpec[], answers: readonly QuestionAnswer[]): Record<string, string> {
  const out: Record<string, string> = {};
  for (const [index, spec] of specs.entries()) {
    const answer = answers[index] ?? null;
    if (answer === null) out[spec.question] = ANSWER_NO_PREFERENCE;
    else if (answer.length === 0) out[spec.question] = ANSWER_NONE_OF_THESE;
    else {
      out[spec.question] = [...answer]
        .sort((a, b) => a - b)
        .map((option) => spec.labels[option]!)
        .join(", ");
    }
  }
  return out;
}

// --- Plans (14.5) ---

export interface PlanSummary {
  title: string;
  steps: string[];
  /** Steps left out. */
  more: number;
  /** The summary is not the whole plan (protocol 14.5 "cut"). */
  cut: boolean;
}

type PlanLine =
  | { kind: "blank" }
  | { kind: "code" }
  | { kind: "table" }
  | { kind: "heading"; text: string }
  | { kind: "item"; indent: number; text: string }
  | { kind: "prose"; text: string };

const FENCE_RE = /^ {0,3}(`{3,}|~{3,})/;
const HEADING_RE = /^ {0,3}#{1,6}(?:\s+(.*?))?\s*$/;
const ITEM_RE = /^(\s*)(?:[-*+]|\d{1,9}[.)])\s+(\S.*)$/;
const TABLE_RE = /^\s*\|/;

function indentOf(spaces: string): number {
  let width = 0;
  for (const ch of spaces) width += ch === "\t" ? 4 - (width % 4) : 1;
  return width;
}

function classify(text: string): PlanLine[] {
  const out: PlanLine[] = [];
  let fence: string | null = null;
  for (const line of text.replace(/\r\n?/g, "\n").split("\n")) {
    const fenceMatch = FENCE_RE.exec(line);
    if (fence !== null) {
      if (fenceMatch && fenceMatch[1]![0] === fence[0] && fenceMatch[1]!.length >= fence.length) fence = null;
      out.push({ kind: "code" });
      continue;
    }
    if (fenceMatch) {
      fence = fenceMatch[1]!;
      out.push({ kind: "code" });
      continue;
    }
    if (line.trim() === "") {
      out.push({ kind: "blank" });
      continue;
    }
    const heading = HEADING_RE.exec(line);
    if (heading) {
      out.push({ kind: "heading", text: (heading[1] ?? "").replace(/\s+#+$/, "").trim() });
      continue;
    }
    if (TABLE_RE.test(line)) {
      out.push({ kind: "table" });
      continue;
    }
    const item = ITEM_RE.exec(line);
    if (item) {
      out.push({ kind: "item", indent: indentOf(item[1]!), text: item[2]! });
      continue;
    }
    out.push({ kind: "prose", text: line.trim() });
  }
  return out;
}

/** Inline Markdown marks: task boxes, links, emphasis, strike-through and code spans. */
function stripMarks(text: string): string {
  return text
    .replace(/^\[[ xX]\]\s+/, "")
    .replace(/!?\[([^\]]*)\]\([^)]*\)/g, "$1")
    .replace(/(\*\*|__)(?=\S)(.+?)(?<=\S)\1/g, "$2")
    .replace(/(^|[^\w*])\*(?=\S)([^*]+?)(?<=\S)\*(?![\w*])/g, "$1$2")
    .replace(/~~(?=\S)(.+?)(?<=\S)~~/g, "$1")
    .replace(/`([^`]+)`/g, "$1")
    .trim();
}

function lineText(line: PlanLine): string {
  return line.kind === "heading" || line.kind === "item" || line.kind === "prose" ? line.text : "";
}

/**
 * Title and steps of an ExitPlanMode plan (protocol 14.5), with `cut` set
 * whenever the summary leaves something of the plan out.
 */
export function summarisePlan(text: string): PlanSummary {
  const lines = classify(text);
  let cut = false;
  const consumed = new Set<number>();

  let titleIndex = lines.findIndex((line) => line.kind === "heading" && line.text !== "");
  if (titleIndex < 0) titleIndex = lines.findIndex((line) => line.kind !== "blank" && line.kind !== "code");
  if (titleIndex >= 0) consumed.add(titleIndex);

  const items = lines.flatMap((line, index) => (line.kind === "item" && index !== titleIndex ? [{ line, index }] : []));
  const rawSteps: string[] = [];
  if (items.length > 0) {
    const top = Math.min(...items.map(({ line }) => line.indent));
    for (const { line, index } of items) {
      if (line.indent !== top) continue;
      rawSteps.push(line.text);
      consumed.add(index);
    }
  } else {
    for (const [index, line] of lines.entries()) {
      if (index <= titleIndex || line.kind !== "prose") continue;
      rawSteps.push(line.text);
      consumed.add(index);
    }
  }
  for (const [index, line] of lines.entries()) {
    if (line.kind !== "blank" && !consumed.has(index)) cut = true;
  }

  const device = (raw: string, maxBytes: number): string => {
    const mapped = toDeviceText(stripMarks(raw));
    if (mapped.replaced > 0) cut = true;
    const value = mapped.text.trim();
    const short = shortenWithEllipsis(value, maxBytes);
    if (short !== value) cut = true;
    return short;
  };

  let title = titleIndex >= 0 ? device(lineText(lines[titleIndex]!), LIMITS.planTitle) : "";
  if (title === "") {
    title = "Plan";
    cut = true;
  }
  const steps: string[] = [];
  for (const raw of rawSteps) {
    const step = device(raw, LIMITS.planStep);
    if (step === "") cut = true;
    else steps.push(step);
  }
  let more = 0;
  if (steps.length > PLAN_MAX_STEPS) {
    more = steps.length - PLAN_MAX_STEPS;
    steps.length = PLAN_MAX_STEPS;
    cut = true;
  }
  return { title, steps, more, cut };
}

/**
 * The `plan` frame. While it is longer than 2048 bytes the last step is
 * dropped, `more` grows and `cut` is set (protocol 14.5 "Overflow").
 */
export function buildPlanFrame(summary: PlanSummary, base: RequestFrameBase): PlanMessage {
  const steps = [...summary.steps];
  let more = summary.more;
  let cut = summary.cut;
  const frame = (): PlanMessage => {
    const content: Obj = { title: summary.title, steps: [...steps], more };
    if (cut) content.cut = true;
    return withBase(base, content, "plan") as unknown as PlanMessage;
  };
  let msg = frame();
  while (frameBytes(msg) > MAX_FRAME_BYTES && steps.length > 0) {
    steps.pop();
    more += 1;
    cut = true;
    msg = frame();
  }
  return msg;
}
