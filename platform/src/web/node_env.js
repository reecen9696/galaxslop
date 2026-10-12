// Node bring-up build: hand the game the host environment's PETARI_* and
// SMG_* switches (Emscripten's ENV starts empty in Node).
Module["preRun"] = Module["preRun"] || [];
Module["preRun"].push(function () {
  for (const k of Object.keys(process.env)) {
    if (k.startsWith("PETARI_") || k.startsWith("SMG_")) ENV[k] = process.env[k];
  }
});
