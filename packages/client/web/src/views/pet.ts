// Pet panel: the mood face plus the agent's current status line.

import { byId, setText } from "../dom.js";
import { MOOD_FACE } from "../format.js";
import type { State } from "../store.js";

export function updatePet(state: State): void {
  const face = byId("pet-face");
  setText(face, MOOD_FACE[state.pet.mood]);
  face.setAttribute("aria-label", `pet looks ${state.pet.mood}`);
  setText(byId("pet-mood"), state.pet.mood);
  setText(byId("pet-status"), state.pet.status ?? "No agent activity yet");
  setText(byId("pet-project"), state.pet.project ?? "");
}
