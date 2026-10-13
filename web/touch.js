// Touch controls for phones and tablets: a thumbstick (the Nunchuk stick)
// bottom left, buttons bottom right, and every other finger on the screen is
// the Wii remote's pointer, so dragging over star bits collects them.  The
// state goes straight into the game's TouchPad (present_web.cpp), read once a
// frame.  The game's rumble comes back out as haptics.
//
// Shown on touch screens (pointer: coarse) or after the first touch; the
// settings checkbox turns it off for good.
"use strict";

// ---- haptics (ported from house/moodboard/haptics.js) ----
// Android: the Vibration API.  iOS (Safari 17.4+): no vibration API, but
// toggling a native switch plays the system "selection" tick; WebKit only
// plays it inside a user gesture, so rumble seen between touches waits for
// the next touch event (dragging fires them steadily).  Desktop: a no-op.
const smgHaptics = (() => {
  const canVibrate = () => typeof navigator.vibrate === "function";
  const isIOS = () => /iPhone|iPad|iPod/.test(navigator.userAgent) || (/Macintosh/.test(navigator.userAgent) && navigator.maxTouchPoints > 1);
  let lastTick = 0, pendingAt = 0;
  function iosTick() {
    const label = document.createElement("label");
    label.setAttribute("aria-hidden", "true");
    label.style.display = "none";
    const input = document.createElement("input");
    input.type = "checkbox";
    input.setAttribute("switch", "");
    input.tabIndex = -1;
    label.append(input);
    document.head.append(label);
    label.click();
    label.remove();
    lastTick = performance.now();
  }
  return {
    // A button under the thumb: very light and crisp.  Call from the touch handler.
    tap() {
      try {
        if (canVibrate()) navigator.vibrate(10);
        else if (isIOS()) iosTick();
      } catch { /* haptics are a nicety; never let them break a touch */ }
    },
    // The game ran the motor for `frames` frames since the last call.
    rumble(frames) {
      try {
        if (canVibrate()) navigator.vibrate(Math.min(frames * 17 + 8, 120));
        else if (isIOS()) pendingAt = performance.now();
      } catch {}
    },
    // From inside a touch event: play rumble that arrived since (iOS).
    flush() {
      if (!pendingAt) return;
      const now = performance.now();
      if (now - pendingAt < 250 && now - lastTick > 60) {
        try { iosTick(); } catch {}
      }
      if (now - pendingAt >= 250 || lastTick >= pendingAt) pendingAt = 0;
    },
  };
})();

(() => {
  // Wii remote bits (present_web.cpp), and the page's own spin bit.
  const kA = 0x0800, kB = 0x0400, kZ = 0x2000, kC = 0x4000, kPlus = 0x0010, kMinus = 0x1000, kSpin = 0x10000;
  const BUTTONS = [
    { id: "tA", label: "A", bit: kA },
    { id: "tB", label: "B", bit: kB },
    { id: "tSpin", label: "Spin", bit: kSpin },
    { id: "tZ", label: "Z", bit: kZ },
    { id: "tC", label: "C", bit: kC },
    { id: "tMinus", label: "−", bit: kMinus },
    { id: "tPlus", label: "+", bit: kPlus },
  ];
  // TouchPad layout (present_web.cpp): f32 stickX, stickY, pointerX, pointerY;
  // u32 held, pressed; i32 active, pointerValid; u32 rumbleFrames.
  const O = { stickX: 0, stickY: 4, pointerX: 8, pointerY: 12, held: 16, pressed: 20, active: 24, pointerValid: 28, rumble: 32 };

  const pad = () => (window.Module && Module._web_touch && Module.HEAPU8 ? Module._web_touch() >>> 0 : 0);
  const i32 = () => new Int32Array(Module.HEAPU8.buffer); // the heap can grow: fetch the view each time
  function write(fields) {
    const p = pad();
    if (!p) return;
    const v = new DataView(Module.HEAPU8.buffer, p, 36);
    for (const [k, val] of Object.entries(fields)) {
      if (k === "stickX" || k === "stickY" || k === "pointerX" || k === "pointerY") v.setFloat32(O[k], val, true);
      else Atomics.store(i32(), (p + O[k]) >>> 2, val);
    }
  }
  function press(bit) {
    const p = pad();
    if (p) Atomics.or(i32(), (p + O.pressed) >>> 2, bit);
  }

  // ---- the overlay ----
  const root = document.createElement("div");
  root.id = "touch";
  root.innerHTML = `<div class="t-stick"><div class="t-knob"></div></div>` +
    BUTTONS.map((b) => `<div id="${b.id}" class="t-btn">${b.label}</div>`).join("");
  document.body.append(root);
  const base = root.querySelector(".t-stick"), knob = root.querySelector(".t-knob");
  const btnEls = BUTTONS.map((b) => ({ ...b, el: root.querySelector("#" + b.id) }));

  // Held upright, the whole page is turned a quarter (body.rotated, index.html)
  // so the game stays landscape.  Events and boxes come in screen space; the
  // controls work in the page's own (landscape) space: x' = y, y' = W - x.
  const rotated = () => document.body.classList.contains("rotated");
  const at = (e) => (rotated() ? { x: e.clientY, y: innerWidth - e.clientX } : { x: e.clientX, y: e.clientY });
  function box(el) {
    const r = el.getBoundingClientRect();
    return rotated() ? { left: r.top, top: innerWidth - r.right, width: r.height, height: r.width } : r;
  }
  const pageW = () => (rotated() ? innerHeight : innerWidth), pageH = () => (rotated() ? innerWidth : innerHeight);

  const fingers = new Map(); // pointerId -> { kind: "stick" | "button" | "aim", bit?, x0?, y0? }
  let held = 0, aimId = null;

  function setHeld() {
    held = 0;
    for (const f of fingers.values()) if (f.kind === "button") held |= f.bit;
    for (const b of btnEls) b.el.classList.toggle("down", (held & b.bit) !== 0);
    write({ held });
  }

  function buttonAt(x, y) {
    // Generous hit areas: the nearest button whose circle, grown by 30%, holds the point.
    let best = null, bestD = Infinity;
    for (const b of btnEls) {
      const r = box(b.el);
      if (!r.width) continue;
      const cx = r.left + r.width / 2, cy = r.top + r.height / 2;
      const d = Math.hypot(x - cx, y - cy);
      if (d < r.width * 0.65 && d < bestD) { best = b; bestD = d; }
    }
    return best;
  }

  // The stick zone: bottom left.  The stick's base jumps to where the thumb lands.
  const inStickZone = (x, y) => x < pageW() * 0.32 && y > pageH() * 0.38;
  const stickRadius = () => box(base).width / 2;

  function moveStick(f, x, y) {
    const r = stickRadius();
    let dx = x - f.x0, dy = y - f.y0;
    const d = Math.hypot(dx, dy);
    if (d > r) { dx *= r / d; dy *= r / d; }
    knob.style.transform = `translate(${dx}px, ${dy}px)`;
    let sx = dx / r, sy = -dy / r;
    if (Math.hypot(sx, sy) < 0.12) sx = sy = 0; // dead zone
    write({ stickX: sx, stickY: sy });
  }

  function aim(x, y) {
    const c = box(document.getElementById("canvas"));
    if (!c.width) return;
    const px = Math.max(-1, Math.min(1, ((x - c.left) / c.width) * 2 - 1));
    const py = Math.max(-1, Math.min(1, ((y - c.top) / c.height) * 2 - 1));
    write({ pointerX: px, pointerY: py, pointerValid: 1 });
  }

  root.addEventListener("pointerdown", (e) => {
    if (e.target.closest?.("#gear, #settings, #roombar")) return;
    e.preventDefault();
    try { root.setPointerCapture(e.pointerId); } catch {}
    const { x, y } = at(e);
    const b = buttonAt(x, y);
    if (b) {
      fingers.set(e.pointerId, { kind: "button", bit: b.bit });
      press(b.bit);
      setHeld();
      smgHaptics.tap();
    } else if (inStickZone(x, y) && ![...fingers.values()].some((f) => f.kind === "stick")) {
      // Keep the whole base on screen.
      const r = stickRadius();
      const x0 = Math.max(r + 8, x), y0 = Math.min(pageH() - r - 8, y);
      fingers.set(e.pointerId, { kind: "stick", x0, y0 });
      base.classList.add("live");
      base.style.left = x0 - r + "px";
      base.style.top = y0 - r + "px";
      base.style.bottom = "auto";
      moveStick(fingers.get(e.pointerId), x, y);
    } else {
      fingers.set(e.pointerId, { kind: "aim" });
      aimId = e.pointerId;
      aim(x, y);
    }
    smgHaptics.flush();
  }, { passive: false });

  root.addEventListener("pointermove", (e) => {
    const f = fingers.get(e.pointerId);
    if (!f) return;
    e.preventDefault();
    const { x, y } = at(e);
    if (f.kind === "stick") moveStick(f, x, y);
    else if (f.kind === "aim" && e.pointerId === aimId) aim(x, y);
    else if (f.kind === "button") {
      // Sliding a thumb onto a neighbouring button presses that one instead (A to B and back).
      const b = buttonAt(x, y);
      if (b && b.bit !== f.bit) {
        f.bit = b.bit;
        press(b.bit);
        setHeld();
        smgHaptics.tap();
      }
    }
    smgHaptics.flush();
  }, { passive: false });

  function release(e) {
    const f = fingers.get(e.pointerId);
    if (!f) return;
    fingers.delete(e.pointerId);
    if (f.kind === "stick") {
      knob.style.transform = "";
      base.classList.remove("live");
      base.style.left = base.style.top = base.style.bottom = "";
      write({ stickX: 0, stickY: 0 });
    } else if (f.kind === "button") {
      setHeld();
    } else if (e.pointerId === aimId) {
      // The pointer stays where the finger left it, like a resting mouse.
      aimId = null;
    }
    smgHaptics.flush();
  }
  root.addEventListener("pointerup", release);
  root.addEventListener("pointercancel", release);
  root.addEventListener("contextmenu", (e) => e.preventDefault());

  // ---- on and off ----
  const coarse = matchMedia("(pointer: coarse)");
  const pref = () => { try { return localStorage.getItem("smg.touch"); } catch { return null; } };
  let enabled = pref() === "1" || (pref() !== "0" && coarse.matches);
  const portrait = matchMedia("(orientation: portrait)");
  // Installed to the home screen the app is landscape only: Android locks it
  // (manifest.webmanifest), iOS can't, so the page turns there, menus too.
  const installed = navigator.standalone === true || matchMedia("(display-mode: standalone), (display-mode: fullscreen)").matches;
  function apply() {
    document.body.classList.toggle("touch", enabled);
    document.body.classList.toggle("rotated", portrait.matches && (installed || enabled && document.body.classList.contains("playing")));
    write({ active: enabled ? 1 : 0 });
    const box = document.getElementById("touchCtl");
    if (box) box.checked = enabled;
  }
  // A touch anywhere turns them on (a laptop with a touch screen), unless switched off.
  addEventListener("pointerdown", (e) => {
    if (e.pointerType === "touch" && !enabled && pref() !== "0") { enabled = true; apply(); }
  }, true);
  document.getElementById("touchCtl")?.addEventListener("change", (e) => {
    enabled = e.target.checked;
    try { localStorage.setItem("smg.touch", enabled ? "1" : "0"); } catch {}
    apply();
  });
  portrait.addEventListener("change", apply);
  apply();

  // Every frame: tell the game the overlay is up (the module loads after this
  // script) and turn its rumble into haptics.
  let wasReady = false;
  function frame() {
    requestAnimationFrame(frame);
    const p = pad();
    if (!p) return;
    if (!wasReady) { wasReady = true; apply(); }
    const n = Atomics.exchange(i32(), (p + O.rumble) >>> 2, 0);
    if (n > 0 && enabled) smgHaptics.rumble(n);
  }
  requestAnimationFrame(frame);
})();
