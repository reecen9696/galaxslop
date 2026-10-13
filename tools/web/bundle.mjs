// Collects the deployable web build into one folder (default dist/web):
//   node tools/web/bundle.mjs [out]
// Contents: the page (web/ with its assets), the wasm build (build/web/smg.{js,wasm}), and the
// cooker's Python (tools/cook -> pycook/).  No game data: players import
// their own disc in the page.  Serve it with the relay (server/, STATIC_DIR)
// or any host that can send the COOP/COEP headers (_headers is included for
// Netlify / Cloudflare Pages).
import { copyFileSync, cpSync, existsSync, mkdirSync, readdirSync, rmSync, writeFileSync } from "node:fs";
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
// The page and everything it loads (cook/, luma, three.js, fonts, music);
// not the dev server or the README.
const skip = new Set(["dev-server.mjs", "README.md"]);
cpSync(join(repo, "web"), out, { recursive: true, filter: (from) => !skip.has(from.slice(join(repo, "web").length + 1)) });
copy("build/web/smg.js", "smg.js");
copy("build/web/smg.wasm", "smg.wasm");
for (const f of readdirSync(join(repo, "tools/cook")).filter((f) => f.endsWith(".py"))) copy(`tools/cook/${f}`, `pycook/${f}`);
copy("tools/rarc.py", "pycook/rarc.py");
writeFileSync(join(out, "_headers"), "/*\n  Cross-Origin-Opener-Policy: same-origin\n  Cross-Origin-Embedder-Policy: require-corp\n  Cross-Origin-Resource-Policy: same-origin\n");
console.log(`web build bundled in ${out}`);
