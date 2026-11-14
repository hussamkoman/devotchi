import { randomBytes } from "node:crypto";
import { chmod, open, rename, unlink } from "node:fs/promises";

export function isErrno(err: unknown, code: string): boolean {
  return typeof err === "object" && err !== null && (err as NodeJS.ErrnoException).code === code;
}

/** Write-then-rename in the same directory, so readers see the old or the new file, never a mix. */
export async function writeFileAtomic(path: string, data: string, mode: number): Promise<void> {
  const tmp = `${path}.${process.pid}.${randomBytes(4).toString("hex")}.tmp`;
  try {
    const handle = await open(tmp, "wx", mode);
    try {
      await handle.writeFile(data, "utf8");
      await handle.sync();
    } finally {
      await handle.close();
    }
    await chmod(tmp, mode);
    await rename(tmp, path);
  } catch (err) {
    await unlink(tmp).catch(() => undefined);
    throw err;
  }
}
