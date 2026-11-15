// Reads the hook's original tool_input (protocol 14.6a) defensively: it is
// untrusted, model-written data of any shape, and only ever goes into the
// page as text.

import type { FullText } from "../store.js";

function record(value: unknown): Record<string, unknown> | null {
  return typeof value === "object" && value !== null && !Array.isArray(value) ? (value as Record<string, unknown>) : null;
}

function input(full: FullText): Record<string, unknown> | null {
  return full.state === "loaded" ? record(full.toolInput) : null;
}

/** The whole command (Bash) or the tool input as indented JSON. */
export function fullCommand(full: FullText, tool: string): string | null {
  const data = input(full);
  if (data === null) return null;
  if (tool === "Bash" && typeof data.command === "string") return data.command;
  return JSON.stringify(data, null, 2);
}

export interface FullQuestion {
  question: string | null;
  header: string | null;
  options: { label: string | null; description: string | null }[];
}

const str = (value: unknown): string | null => (typeof value === "string" && value !== "" ? value : null);

/** AskUserQuestion `questions`, entry by entry; fields that are not strings are null. */
export function fullQuestions(full: FullText): FullQuestion[] | null {
  const questions = input(full)?.questions;
  if (!Array.isArray(questions)) return null;
  return questions.map((q) => {
    const entry = record(q);
    const options = Array.isArray(entry?.options) ? entry.options : [];
    return {
      question: str(entry?.question),
      header: str(entry?.header),
      options: options.map((o) => ({ label: str(record(o)?.label), description: str(record(o)?.description) })),
    };
  });
}

/** ExitPlanMode `plan`, the Markdown text as written. */
export function fullPlan(full: FullText): string | null {
  return str(input(full)?.plan);
}
