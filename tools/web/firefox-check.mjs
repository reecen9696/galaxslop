// Boots the web build in headless Firefox (Playwright) and reports progress;
// tools/web/smoke.mjs covers Chrome.  Needs Playwright (not a repo
// dependency): npm i playwright && npx playwright install firefox
//   node tools/web/firefox-check.mjs [url] [folder to import] [seconds]
// With a folder, the page's import screen gets it (cooked or extracted disc).
import { firefox } from "playwright";

const [url = "http://localhost:8080/?solo=1&PETARI_TIMER=1", dir, secs = "60"] = process.argv.slice(2);
const browser = await firefox.launch({ headless: true });
const page = await browser.newPage();
const log = [];
page.on("console", (m) => log.push(m.text()));
page.on("pageerror", (e) => log.push("PAGEERROR " + e.message));
await page.goto(url);
if (dir) {
  await page.waitForSelector("#import", { state: "visible", timeout: 20000 });
  await page.setInputFiles("#pickInput", dir);
}
for (let t = 0; t < Number(secs); t += 5) {
  await page.waitForTimeout(5000);
  console.log(`${t + 5} s: ${await page.evaluate(() => document.getElementById("status").textContent)}`);
}
const ok = log.some((l) => /display frames/.test(l)) && !log.some((l) => /no new game frame|PAGEERROR|RuntimeError/.test(l));
for (const l of log.filter((l) => /PAGEERROR|RuntimeError|failed|no new game frame/.test(l)).slice(0, 10)) console.log("  " + l);
await browser.close();
console.log(ok ? "firefox: ok" : "firefox: FAIL");
process.exit(ok ? 0 : 1);
