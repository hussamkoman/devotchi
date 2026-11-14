// Display helpers shared by the views.

import type { Mood } from "@devotchi/shared";

export const MOOD_FACE: Record<Mood, string> = {
  happy: "\u{1F60A}",
  sad: "\u{1F622}",
  excited: "\u{1F929}",
  anxious: "\u{1F630}",
  sleeping: "\u{1F634}",
  angry: "\u{1F620}",
  proud: "\u{1F60E}",
  idle: "\u{1F642}",
  focused: "\u{1F9D0}",
  working: "\u{1F4BB}",
  thinking: "\u{1F914}",
};

export function relativeTime(iso: string | null, now: number): string {
  if (iso === null) return "never";
  const s = Math.max(0, Math.round((now - Date.parse(iso)) / 1000));
  if (s < 5) return "just now";
  if (s < 60) return `${s} s ago`;
  if (s < 3600) return `${Math.floor(s / 60)} min ago`;
  if (s < 86400) return `${Math.floor(s / 3600)} h ago`;
  return `${Math.floor(s / 86400)} d ago`;
}

export function seconds(ms: number): string {
  const s = Math.max(0, Math.ceil(ms / 1000));
  return s >= 60 ? `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}` : `${s} s`;
}

export function holdLabel(ms: number): string {
  return ms >= 60_000 ? `${ms / 60_000} min` : `${ms / 1000} s`;
}
