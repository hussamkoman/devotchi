// @vitest-environment happy-dom
import { describe, expect, it } from "vitest";
import { initialState, reduce, type State } from "../../web/src/store.js";
import { RequestList } from "../../web/src/views/requests.js";

const id = (n: number) => n.toString(16).padStart(32, "0");

function withRequests(n: number): State {
  let state = reduce(initialState(), { type: "connection", phase: "open", now: 0 });
  for (let i = 1; i <= n; i++) {
    state = reduce(state, {
      type: "frame",
      now: 0,
      msg: { type: "permission", id: id(i), tool: "Bash", summary: `cmd ${i}`, risk: "low", timeout: 30_000, timestamp: new Date(0).toISOString() },
    });
  }
  return state;
}

describe("request list rendering", () => {
  it("leaves rendered cards in place on later updates, so focus inside a card survives the countdown tick", () => {
    const list = document.createElement("div");
    const empty = document.createElement("p");
    document.body.append(list, empty);
    const view = new RequestList(list, empty, { answer: () => undefined });
    const state = withRequests(2);
    view.update(state, 0);
    expect([...list.children].map((c) => c.getAttribute("data-id"))).toEqual([id(1), id(2)]);

    const mutations: MutationRecord[] = [];
    const observer = new MutationObserver((records) => mutations.push(...records));
    observer.observe(list, { childList: true });
    view.update(state, 500);
    view.update(state, 1000);
    expect(observer.takeRecords().concat(mutations).filter((m) => m.type === "childList")).toEqual([]);
    observer.disconnect();
  });
});
