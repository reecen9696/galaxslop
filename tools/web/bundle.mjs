// Collects the deployable web build into one folder (default dist/web):
//   node tools/web/bundle.mjs [out]
// Contents: the page (web/), the wasm build (build/web/smg.{js,wasm}), and the
// cooker's Python (tools/cook -> pycook/).  No game data: players import
// their own disc in the page.  Serve it with the relay (server/, STATIC_DIR)
// or any host that can send the COOP/COEP headers (_headers is included for
// Netlify / Cloudflare Pages).
import { copyFileSync, existsSync, mkdirSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repo = resolve(fileURLToPath(import.meta.url), "../../..");
const out = resolve(process.argv[2] ?? join(repo, "dist/web"));
const need = ["build/web/smg.js", "build/web/smg.wasm"];
for (const f of need) {
  if (!existsSync(join(repo, f))) {
    console.error(`missing ${f}: build the browser variant first (docs/WEB.md)`);
    process.exit(1);
  }
}
rmSync(out, { recursive: true, force: true });
const copy = (from, to) => {
  mkdirSync(dirname(join(out, to)), { recursive: true });
  copyFileSync(join(repo, from), join(out, to));
};
copy("web/index.html", "index.html");
copy("web/app.js", "app.js");
for (const f of readdirSync(join(repo, "web/cook"))) copy(`web/cook/${f}`, `cook/${f}`);
copy("build/web/smg.js", "smg.js");
copy("build/web/smg.wasm", "smg.wasm");
for (const f of readdirSync(join(repo, "tools/cook")).filter((f) => f.endsWith(".py"))) copy(`tools/cook/${f}`, `pycook/${f}`);
copy("tools/rarc.py", "pycook/rarc.py");
writeFileSync(join(out, "_headers"), "/*\n  Cross-Origin-Opener-Policy: same-origin\n  Cross-Origin-Embedder-Policy: require-corp\n  Cross-Origin-Resource-Policy: same-origin\n");
console.log(`web build bundled in ${out}`);
