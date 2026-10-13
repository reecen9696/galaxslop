// Luma dancing above the loading bar on the first visit (web/app.js
// showLoading).  three.js (MIT, web/vendor/three) is loaded only here.
// The model, web/luma/luma.fbx, is a fan-made Sketchfab model of a Nintendo
// character with no license given, so it is not in git (D-024): without it
// the loading screen just shows the bar.
//
// The model's own "Luma Idle" clip plays underneath; the dance on top is
// procedural: hops with squash and stretch, a sway, travel back and forth
// across the bar, a spin every few seconds, a shadow and sparkles.
import * as THREE from "three";
import { FBXLoader } from "./vendor/three/examples/jsm/loaders/FBXLoader.js";

const kYellow = 0xffd93b, kGlow = 0xffa600;

function softDot(size, inner, outer) {
  const c = document.createElement("canvas");
  c.width = c.height = size;
  const g = c.getContext("2d"), r = size / 2;
  const grad = g.createRadialGradient(r, r, 0, r, r, r);
  grad.addColorStop(0, inner);
  grad.addColorStop(1, outer);
  g.fillStyle = grad;
  g.fillRect(0, 0, size, size);
  return new THREE.CanvasTexture(c);
}

export async function startLuma(holder) {
  const r = await fetch("luma/luma.fbx");
  if (!r.ok) throw new Error(`luma/luma.fbx: HTTP ${r.status}`);
  const model = new FBXLoader().parse(await r.arrayBuffer(), "luma/");

  const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, powerPreference: "low-power" });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  holder.appendChild(renderer.domElement);
  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(28, 2, 0.1, 100);
  // Ground (y = 0) near the bottom of the frame, just above the bar.
  camera.position.set(0, 1.8, 7);
  camera.lookAt(0, 1.5, 0);

  scene.add(new THREE.HemisphereLight(0xfff6d8, 0x2a3a66, 2.2));
  const key = new THREE.DirectionalLight(0xffffff, 2.4);
  key.position.set(2, 4, 5);
  scene.add(key);
  const rim = new THREE.DirectionalLight(0x9fd4ff, 1.6);
  rim.position.set(-3, 2, -4);
  scene.add(rim);

  // The export's body colour is green; Luma is yellow and glows a little.
  model.traverse((o) => {
    if (!o.isMesh) return;
    o.material = [].concat(o.material).map((m) =>
      /eye/i.test(m.name)
        ? new THREE.MeshPhongMaterial({ name: m.name, color: 0x05070d, specular: 0x8899aa, shininess: 90 })
        : new THREE.MeshPhongMaterial({ name: m.name, color: kYellow, emissive: kGlow, emissiveIntensity: 0.35, specular: 0xfff2c0, shininess: 40 }));
    if (o.material.length === 1) o.material = o.material[0];
  });
  // Unit height, feet at y = 0, centred.
  const box = new THREE.Box3().setFromObject(model);
  const size = box.getSize(new THREE.Vector3()), centre = box.getCenter(new THREE.Vector3());
  const s = 1.15 / size.y;
  model.scale.setScalar(s);
  model.position.set(-centre.x * s, -box.min.y * s, -centre.z * s);
  const dancer = new THREE.Group();
  dancer.add(model);
  scene.add(dancer);

  const mixer = new THREE.AnimationMixer(model);
  const clips = model.animations;
  const idle = clips.find((c) => c.name === "Luma Idle") || clips.find((c) => /idle/i.test(c.name)) || clips.reduce((a, c) => (c.duration > (a?.duration ?? 0) ? c : a), null);
  if (idle) mixer.clipAction(idle).play();

  // A warm glow under Luma (a dark shadow vanishes against space).
  const shadow = new THREE.Mesh(new THREE.PlaneGeometry(1.3, 1.3), new THREE.MeshBasicMaterial({ map: softDot(64, "rgba(255,214,90,0.55)", "rgba(255,180,40,0)"), transparent: true, depthWrite: false, blending: THREE.AdditiveBlending }));
  shadow.rotation.x = -Math.PI / 2;
  scene.add(shadow);

  // Sparkles shed while dancing: rise, drift and fade.
  const kSparks = 40;
  const sparkPos = new Float32Array(kSparks * 3), sparkVel = new Float32Array(kSparks * 3), sparkLife = new Float32Array(kSparks);
  const sparkGeo = new THREE.BufferGeometry();
  sparkGeo.setAttribute("position", new THREE.BufferAttribute(sparkPos, 3));
  const sparkMat = new THREE.PointsMaterial({ size: 0.2, map: softDot(32, "rgba(255,250,200,1)", "rgba(255,210,60,0)"), transparent: true, depthWrite: false, blending: THREE.AdditiveBlending });
  scene.add(new THREE.Points(sparkGeo, sparkMat));
  let nextSpark = 0;

  function resize() {
    const w = holder.clientWidth, h = holder.clientHeight;
    if (!w || !h) return;
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  }
  const ro = new ResizeObserver(resize);
  ro.observe(holder);
  resize();

  const clock = new THREE.Clock();
  let raf = 0, facing = 0;
  const kBeat = 0.48; // seconds per hop
  function frame() {
    raf = requestAnimationFrame(frame);
    const dt = Math.min(clock.getDelta(), 0.05), t = clock.elapsedTime;
    mixer.update(dt);
    // Travel: across the bar and back, about 11 s a round trip.
    const range = Math.min(1.7, camera.aspect * 0.75);
    const x = Math.sin(t * 0.57) * range, vx = Math.cos(t * 0.57);
    const hop = Math.abs(Math.sin((t * Math.PI) / kBeat));
    const land = 1 - hop; // 1 on the ground
    dancer.position.set(x, hop * 0.34, 0);
    // Squash on landing, stretch in the air.
    const sq = 1 - 0.16 * Math.pow(land, 6) + 0.06 * hop;
    dancer.scale.set(1 / Math.sqrt(sq), sq, 1 / Math.sqrt(sq));
    // Face the way it travels, turning to the camera now and then; spin
    // all the way round every 6 s.
    const want = vx * 0.9 * (0.5 + 0.5 * Math.sin(t * 0.9));
    facing += (want - facing) * Math.min(1, dt * 4);
    const spinT = (t % 6) / 6, spin = spinT > 0.85 ? ((spinT - 0.85) / 0.15) * Math.PI * 2 : 0;
    dancer.rotation.set(0, facing + spin, Math.sin((t * Math.PI) / kBeat / 2) * 0.16);
    shadow.position.set(x, 0.01, 0);
    shadow.scale.setScalar(0.95 - hop * 0.35);
    shadow.material.opacity = 0.9 - hop * 0.5;
    // Sparkles.
    if (t >= nextSpark) {
      const i = Math.floor(Math.random() * kSparks);
      sparkPos.set([x + (Math.random() - 0.5) * 0.6, dancer.position.y + 0.4 + Math.random() * 0.5, (Math.random() - 0.5) * 0.4], i * 3);
      sparkVel.set([(Math.random() - 0.5) * 0.3, 0.35 + Math.random() * 0.4, 0], i * 3);
      sparkLife[i] = 1;
      nextSpark = t + 0.05;
    }
    for (let i = 0; i < kSparks; i++) {
      if (sparkLife[i] <= 0) {
        sparkPos[i * 3 + 1] = -99;
        continue;
      }
      sparkLife[i] -= dt * 0.8;
      for (let k = 0; k < 3; k++) sparkPos[i * 3 + k] += sparkVel[i * 3 + k] * dt;
    }
    sparkGeo.attributes.position.needsUpdate = true;
    sparkMat.opacity = 0.9;
    renderer.render(scene, camera);
  }
  frame();

  return {
    stop() {
      cancelAnimationFrame(raf);
      ro.disconnect();
      renderer.dispose();
      renderer.forceContextLoss();
      renderer.domElement.remove();
    },
  };
}
