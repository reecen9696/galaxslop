// Browser build: URL parameters named PETARI_* or SMG_* become environment
// variables for the game (?PETARI_INPUT=28:A|B&SMG_NAME=Reece ...).
Module["preRun"] = Module["preRun"] || [];
Module["preRun"].push(function () {
  // The lobby's choices (web/app.js), then the URL, which wins.
  for (const [k, v] of Object.entries(self.smgEnv || {})) ENV[k] = v;
  const params = new URLSearchParams(self.location.search);
  for (const [k, v] of params) {
    if (k.startsWith("PETARI_") || k.startsWith("SMG_")) ENV[k] = v;
  }
});
