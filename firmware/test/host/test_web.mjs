// test_web.mjs — the browser side and everything that must agree with the firmware:
// the web UI (data/), the routes in src/main.cpp, docs/rest-api.md, and the web
// flasher (docs/flash/) with the prebuilt images. Node only, no dependencies.
//
//   node test_web.mjs
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import zlib from "node:zlib";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const FW = path.resolve(HERE, "../..");
const REPO = path.resolve(FW, "..");
const DATA = path.join(FW, "data");
const read = (p) => fs.readFileSync(p, "utf8");

let pass = 0, fail = 0;
const check = (ok, what) => { console.log(`   ${ok ? "ok  " : "FAIL"}  ${what}`); ok ? pass++ : fail++; };
const section = (s) => console.log(`\n-- ${s} --`);

const mainCpp = read(path.join(FW, "src/main.cpp"));
const routes = new Set([...mainCpp.matchAll(/g_portal\.on\("([^"]+)"/g)].map((m) => m[1]));
const app = read(path.join(DATA, "app.html"));

// ---------------------------------------------------------------------------------------
section("web assets parse");
for (const f of fs.readdirSync(DATA).filter((f) => f.endsWith(".html"))) {
  const html = read(path.join(DATA, f));
  const scripts = [...html.matchAll(/<script(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
  let ok = true, err = "";
  scripts.forEach((s, i) => { try { new vm.Script(s, { filename: `${f} script ${i + 1}` }); } catch (e) { ok = false; err = e.message; } });
  check(ok, `${f}: ${scripts.length} inline script(s) parse ${err}`);
}
try { new vm.Script(read(path.join(DATA, "sw.js"))); check(true, "sw.js parses"); } catch (e) { check(false, `sw.js: ${e.message}`); }
let manifest;
try { manifest = JSON.parse(read(path.join(DATA, "manifest.json"))); check(true, "manifest.json is valid JSON"); } catch (e) { check(false, `manifest.json: ${e.message}`); }
if (manifest) {
  const icons = (manifest.icons || []).map((i) => i.src.replace(/^\//, ""));
  check(icons.length > 0 && icons.every((i) => fs.existsSync(path.join(DATA, i))), `manifest icons exist (${icons.join(", ")})`);
  check(icons.every((i) => routes.has("/" + i)), "…and the firmware serves them");
}
const ex = JSON.parse(read(path.join(DATA, "wifi.json.example")));
check(typeof ex.ssid === "string" && typeof ex.pass === "string", "wifi.json.example has the ssid/pass keys tryProvisionedWiFi() reads");
const sw = read(path.join(DATA, "sw.js"));
const cached = [...sw.matchAll(/"(\/[^"]+)"/g)].map((m) => m[1]).filter((p) => /\.(ico|png|json)$/.test(p));
check(cached.length > 0 && cached.every((p) => routes.has(p) && fs.existsSync(path.join(DATA, p.slice(1)))), `service worker pre-caches only files that exist and are served (${cached.join(", ")})`);

section("compressed copies are current");
for (const f of fs.readdirSync(DATA).filter((f) => f.endsWith(".gz"))) {
  const src = path.join(DATA, f.slice(0, -3));
  const same = fs.existsSync(src) && zlib.gunzipSync(fs.readFileSync(path.join(DATA, f))).equals(fs.readFileSync(src));
  check(same, `${f} matches ${path.basename(src)} (else: gzip -9 -k -f data/${path.basename(src)})`);
}

// ---------------------------------------------------------------------------------------
section("UI <-> firmware");
const ids = new Set([...app.matchAll(/\bid="([^"]+)"/g)].map((m) => m[1]));
const used = new Set([...app.matchAll(/getElementById\("([^"]+)"\)/g)].map((m) => m[1]));
const missing = [...used].filter((i) => !ids.has(i));
check(missing.length === 0, `every getElementById() names an element in app.html (${used.size} ids) ${missing.join(" ")}`);
const calls = new Set([...app.matchAll(/(?:api|fetch)\(\s*"(\/[^"?]*)/g)].map((m) => m[1]));
const unknown = [...calls].filter((c) => !routes.has(c));
check(calls.size > 10 && unknown.length === 0, `every path the UI calls is a firmware route (${calls.size} paths) ${unknown.join(" ")}`);
check(/discovery:\s*document\.getElementById\("mqttDiscovery"\)\.checked/.test(app) && /getElementById\("mqttDiscovery"\)\.checked\s*=\s*!!c\.discovery/.test(app),
  "the Tasmota discovery toggle is loaded from and saved to /api/mqtt");
for (const k of ["enabled", "retain", "commands", "discovery", "host", "port", "user", "prefix", "device_name"])
  check(new RegExp(`boolField\\("${k}"|apiStrParam\\("${k}"|apiIntParam\\("${k}"`).test(mainCpp), `/api/mqtt accepts "${k}" (the UI sends it)`);

section("templates");
const info = read(path.join(DATA, "info.html"));
const holders = [...new Set([...info.matchAll(/%([A-Z_]+)%/g)].map((m) => m[1]))];
const filled = holders.filter((h) => mainCpp.includes(`h.replace("%${h}%"`));
check(holders.length > 10 && filled.length === holders.length, `/info fills every placeholder in info.html (${holders.length}) ${holders.filter((h) => !filled.includes(h)).join(" ")}`);
check(read(path.join(DATA, "old.html")).includes("%LABEL%") && mainCpp.includes('html.replace("%LABEL%"'), "old.html has %LABEL% and the firmware fills it");

// ---------------------------------------------------------------------------------------
section("docs");
const restDoc = read(path.join(REPO, "docs/rest-api.md"));
const apiRoutes = [...routes].filter((r) => r.startsWith("/api/"));
const undocumented = apiRoutes.filter((r) => !restDoc.includes(r));
check(undocumented.length === 0, `every /api route is in docs/rest-api.md (${apiRoutes.length}) ${undocumented.join(" ")}`);
const mqttDoc = read(path.join(REPO, "docs/mqtt.md"));
const mqttH = read(path.join(FW, "src/mqtt.h"));
const nativeTopics = [...mqttH.matchAll(/topic\("([a-z/]+)"\)/g)].map((m) => m[1]).filter((t) => t !== "#");
const undocTopics = [...new Set(nativeTopics)].filter((t) => !mqttDoc.includes(`<prefix>/${t}`));
check(undocTopics.length === 0, `every native MQTT topic is in docs/mqtt.md ${undocTopics.join(" ")}`);
check(mqttDoc.includes("tasmota/discovery/<MAC>/config") && mqttDoc.includes("cmnd/<t>/POWER<n>"), "docs/mqtt.md documents Tasmota discovery");

// ---------------------------------------------------------------------------------------
section("web flasher and prebuilt images");
const flashDir = path.join(REPO, "docs/flash");
const fm = JSON.parse(read(path.join(flashDir, "manifest.json")));
const pm = JSON.parse(read(path.join(FW, "prebuilt/manifest.json")));
check(JSON.stringify(fm) === JSON.stringify(pm), "docs/flash/manifest.json == firmware/prebuilt/manifest.json");
const parts = fm.builds[0].parts;
check(fm.builds[0].chipFamily === "ESP8266" && parts.length === 2, "one ESP8266 build, two parts");
for (const p of parts) {
  const a = path.join(flashDir, p.path), b = path.join(FW, "prebuilt", p.path);
  check(fs.existsSync(a) && fs.existsSync(b) && fs.readFileSync(a).equals(fs.readFileSync(b)), `${p.path}: flasher copy == prebuilt copy`);
}
const ini = read(path.join(FW, "platformio.ini"));
const fsPart = parts.find((p) => p.path === "littlefs.bin");
check(/eagle\.flash\.4m2m\.ld/.test(ini) && fsPart && fsPart.offset === 0x200000, "littlefs.bin at 0x200000, where the 4m2m layout in platformio.ini mounts it");
const fw = fs.readFileSync(path.join(FW, "prebuilt/firmware.bin"));
check(fw[0] === 0xe9 && fw.length < 1044464, "firmware.bin is an ESP8266 image (0xE9) that fits the app partition");
const flashHtml = read(path.join(flashDir, "index.html"));
check(flashHtml.includes('manifest="manifest.json"') || flashHtml.includes("manifest.json"), "the flasher page points at manifest.json");

console.log(`\nweb: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
