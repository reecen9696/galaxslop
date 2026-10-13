import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { startServer } from "../src/main.ts";

describe("static hosting (STATIC_DIR)", () => {
  let server: ReturnType<typeof startServer>;
  let base: string;
  beforeAll(async () => {
    const dir = mkdtempSync(join(tmpdir(), "smg-static-"));
    writeFileSync(join(dir, "index.html"), "<!doctype html><title>t</title>");
    writeFileSync(join(dir, "smg.wasm"), new Uint8Array([0, 97, 115, 109]));
    process.env.STATIC_DIR = dir;
    server = startServer(0, () => {});
    await new Promise((r) => setTimeout(r, 50));
    base = `http://localhost:${server.port()}`;
  });
  afterAll(async () => {
    delete process.env.STATIC_DIR;
    await server.close();
  });

  it("serves the page with the cross-origin isolation headers", async () => {
    const r = await fetch(base + "/");
    expect(r.status).toBe(200);
    expect(r.headers.get("cross-origin-opener-policy")).toBe("same-origin");
    expect(r.headers.get("cross-origin-embedder-policy")).toBe("require-corp");
    expect(await r.text()).toContain("<title>t</title>");
  });

  it("serves wasm with its type", async () => {
    const r = await fetch(base + "/smg.wasm");
    expect(r.headers.get("content-type")).toBe("application/wasm");
  });

  it("tells the page the relay is on this origin", async () => {
    expect(await (await fetch(base + "/config.json")).json()).toEqual({ relay: "same-origin" });
  });

  it("refuses paths outside the folder", async () => {
    expect((await fetch(base + "/..%2f..%2fetc%2fpasswd")).status).toBe(404);
    expect((await fetch(base + "/missing.js")).status).toBe(404);
  });
});
