import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { HOLDING_INTERVAL_MS } from "@devotchi/shared";
import { HoldingDriver } from "../../web/src/holding.js";

const id = (n: number) => n.toString(16).padStart(32, "0");

describe("dashboard holding", () => {
  const doc = { visibilityState: "visible" };
  let sent: string[][];
  let driver: HoldingDriver;
  let ids: string[];
  const add = (n: number) => {
    ids = [...ids, id(n)];
    driver.sync(ids);
  };

  beforeEach(() => {
    vi.useFakeTimers();
    doc.visibilityState = "visible";
    sent = [];
    ids = [];
    driver = new HoldingDriver({ send: (x) => sent.push(x), visible: () => doc.visibilityState === "visible" });
  });
  afterEach(() => {
    driver.reset();
    vi.useRealTimers();
  });

  it("acknowledges three permissions within 50 ms with one frame naming all three", () => {
    add(1);
    vi.advanceTimersByTime(25);
    add(2);
    vi.advanceTimersByTime(25);
    add(3);
    vi.advanceTimersByTime(99);
    expect(sent).toEqual([]);
    vi.advanceTimersByTime(1);
    expect(sent).toEqual([[id(1), id(2), id(3)]]);
    vi.advanceTimersByTime(HOLDING_INTERVAL_MS);
    expect(sent).toHaveLength(2);
    expect(sent[1]).toEqual([id(1), id(2), id(3)]);
  });

  it("never acks later than 500 ms after the first request of a burst", () => {
    for (let n = 1; n <= 8; n++) {
      add(n);
      vi.advanceTimersByTime(80);
    }
    expect(sent).toHaveLength(1);
    expect(sent[0]).toHaveLength(7);
  });

  it("names nothing while the tab is hidden and acks when it becomes visible", () => {
    doc.visibilityState = "hidden";
    add(1);
    vi.advanceTimersByTime(10_000);
    expect(sent).toEqual([]);
    doc.visibilityState = "visible";
    driver.visibilityChanged();
    vi.advanceTimersByTime(100);
    expect(sent).toEqual([[id(1)]]);
    doc.visibilityState = "hidden";
    driver.visibilityChanged();
    vi.advanceTimersByTime(3 * HOLDING_INTERVAL_MS);
    expect(sent).toHaveLength(1);
  });

  it("stops naming a request once it left the list", () => {
    add(1);
    add(2);
    vi.advanceTimersByTime(100);
    ids = [id(2)];
    driver.sync(ids);
    vi.advanceTimersByTime(HOLDING_INTERVAL_MS);
    expect(sent.at(-1)).toEqual([id(2)]);
    driver.sync([]);
    vi.advanceTimersByTime(3 * HOLDING_INTERVAL_MS);
    expect(sent).toHaveLength(2);
  });
});
