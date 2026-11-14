import { DatabaseSync } from "node:sqlite";
import { describe, expect, it } from "vitest";

// Some vite-node versions strip the "node:" prefix from builtin imports; sqlite
// has no unprefixed builtin, so this import only works if the prefix survives.
describe("node:sqlite inside vitest", () => {
  it("opens an in-memory database", () => {
    const db = new DatabaseSync(":memory:");
    db.exec("CREATE TABLE t (v INTEGER)");
    db.prepare("INSERT INTO t (v) VALUES (?)").run(42);
    const row = db.prepare("SELECT v FROM t").get() as { v: number };
    expect(row.v).toBe(42);
    db.close();
  });
});
