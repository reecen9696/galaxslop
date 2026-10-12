# Agent rules

These rules apply to every AI agent working in this repo: Claude Code,
Gemini CLI, or anything else. `CLAUDE.md` and `GEMINI.md` both point here.
Read this file in full at the start of every session. Then load
`skills/smg-coop/SKILL.md`.

## 1. The two-agent problem

Two agents work on this repo at the same time, on different machines,
driven by different people. Neither can see the other's uncommitted work.
Every rule below exists to stop them colliding.

### Lanes

Each agent has a lane. Lanes own directories. You only edit files in your
lane's directories unless a task in `TASKS.md` explicitly says otherwise.

| Lane | Default driver | Owns |
|---|---|---|
| A | Claude | `platform/src/gx/`, `platform/src/vi/`, `platform/src/audio/`, `platform/src/web/`, `web/`, `tools/bench/` |
| B | OpenClaude | `platform/src/os/`, `platform/src/dvd/`, `platform/src/nand/`, `platform/src/input/`, `platform/src/sdk/`, `platform/src/port/`, `platform/src/native/`, `net/`, `server/`, `tools/cook/`, `tools/cook-ts/` |
| shared | both, by task | `decomp/`, `platform/include/`, `CMakeLists.txt`, `docs/`, `TASKS.md`, `AGENTS.md`, `skills/` |

Paths follow GalaxyQuest's `platform/src/<subsystem>/` layout (D-007).
Lane A's Phase 0 is the GX layer (P0-11, fallback P0-10); lane B's is the host, OS,
disc and cooker side. They don't share files until the link step.

`decomp/` is shared because both lanes need `TARGET_PC` hooks in game
code. Rule: a task in `decomp/` names the exact files it touches. Two open
tasks must not list the same file.

### Claiming work

1. Read `TASKS.md`. Find the first task that is `status: open`, in your
   lane (or `shared`), whose `blocked_by` list is all `done`.
2. Edit that task: set `owner` to your lane letter and `status` to
   `in_progress`. Add today's date to `started`.
3. Commit only that change to `main` with message
   `board: claim <TASK-ID>`. Push. If the push is rejected, pull, re-read
   the board, and pick again. Someone else got there first.
4. Now create your branch and start.

Never claim more than two tasks at once. Never start a task without
claiming it on `main` first. A claim on a branch is invisible to the other
agent.

### Branches and merges

- Branch name: `<lane>/<task-id>-<short-name>`. Lowercase. Example:
  `b/p1-04-opfs-reader`.
- One task per branch. One branch per task.
- Rebase on `main` before opening a PR. Resolve conflicts yourself.
- PR title: `<TASK-ID>: <what changed>`. PR body: what, why, how tested,
  what the reviewer should look at.
- The other lane's agent reviews. Review means: build it, run the tests,
  read the diff. Approve or request changes in the PR.
- Merge with squash. Delete the branch.
- Never force-push `main`. Never rewrite history on a shared branch.
- Never merge your own PR unless the board says the task is `solo` (small
  tasks where review would cost more than it's worth).

### Board updates

`TASKS.md` is the single source of truth for status. Update it in the same
PR as the code, in these transitions:

- `open` -> `in_progress` (claim, on `main`, see above)
- `in_progress` -> `review` (in the PR)
- `review` -> `done` (by the reviewer, in the merge)
- any -> `blocked` with a `blocked_on:` line saying why, and a note in the
  task's `log`

Every task has a `log`. Append dated one-line entries. Decisions, dead
ends, things the next agent needs to know. Keep each line short.

### Handoffs

If you stop mid-task, write a `log` entry saying where you are, what works,
what doesn't, and what to do next. Push the branch. Set `status: paused`.
The other agent may pick it up.

## 2. Code rules

- Language: C++20 (GalaxyQuest's baseline) for `platform/`, `net/` and
  `decomp/` changes. TypeScript for `web/`, `server/` and `tools/cook-ts/`.
  Python 3.10+ for the rest of `tools/`.
- Every change to `decomp/` is wrapped in `#ifdef TARGET_PC` or
  `#ifdef TARGET_WEB`. The original Wii code path must still compile in
  spirit. Keep the diff against Petari minimal and greppable.
- No new third-party dependency without a `DECISIONS.md` entry explaining
  why and checking the license (MIT/BSD/Unlicense/CC0 only).
- Tests: `platform/` and `net/` get unit tests (doctest). `server/` gets
  vitest. Every PR adds or updates a test for what it changed, or says why
  it can't.
- Performance: the web build must hold 60 fps at 1280x720 on the reference
  machine in `docs/BUILD_SPEC.md`. A PR that drops a benchmark in
  `tools/bench/` by more than 10% needs a `DECISIONS.md` entry.
- No game data anywhere in git, in CI, or in any hosted artifact. No
  `.data`, `.arc`, `.rarc`, `.szs`, `.bmd`, `.bdl`, `.brres`, `.dol`,
  `.iso`, `.rvz`, `.wbfs`. `.gitignore` enforces this. If you need a test
  asset, generate a synthetic one in the test.
- No secrets in git. The relay server reads config from env.

## 3. Decisions

Architecture decisions go in `docs/DECISIONS.md`, append-only, numbered,
dated, with the lane that made it. Small ones get three lines. Format:

```
## D-021 (2026-11-03, lane B): Cache OPFS sync handles for the session
Why: opening a handle per read costs ~0.3 ms; the DVD layer reads often.
Revisit: if open handles ever exceed the browser's per-origin limit.
```

Do not reverse a decision in code without a new entry that supersedes it.

## 4. Upstream projects

- Petari (SMGCommunity) and AstroCore (Luma's Workshop) do not accept
  AI-generated decompilation or code. Never open a PR against them from
  this repo. If you find a real bug in Petari's logic, write it up in
  `docs/UPSTREAM_NOTES.md` so a human can report it in their own words.
- GalaxyQuest (bigmak94) is Unlicense and AI-built. Pulling fixes from it
  is fine. Note the commit hash in `DECISIONS.md`.
- Aurora (encounter) is MIT. It is a pinned submodule in `extern/aurora`;
  don't edit it in place. Fixes go upstream as PRs under your own name
  with a human reviewing, or into `platform/src/gx/` as a shim. Dawn, SDL3,
  nod and kabufuda arrive through Aurora's providers; they keep their own
  licenses (all permissive).
- webcompiled and WiiCompiled are GPL-3. Read their Emscripten/Aurora
  build setup for the approach. Copy nothing. This repo stays Unlicense.
- smg-online: FimeV1's own code is MIT. Headpenguin's code in its
  `SMGNetworkMultiplayer/` folder has no license. Do not copy it. Read it
  for ideas only.
- noclip.website: check its license before copying any code.

## 5. Scope guard

The spec is in `docs/BUILD_SPEC.md`. Phases are ordered. Don't start
Phase N+1 work while Phase N has open blockers, unless the task is marked
`parallel-ok`. Don't add features the spec doesn't list. If you think the
spec is wrong, say so in a `DECISIONS.md` proposal and stop.

## 6. What to do when stuck

1. Check the task's `log` and `DECISIONS.md`.
2. Check `docs/BUILD_SPEC.md` section 9 (known problems).
3. Check GalaxyQuest's `docs/TECHNICAL.md` for how they solved it.
   (SMGwebport has no source; don't go looking.)
4. Write a `log` entry with what you tried. Set `status: blocked`. Move to
   another task. Don't burn hours on one wall.

## 7. Session checklist

Start:
- `git checkout main && git pull`
- Read `AGENTS.md` (this file), `TASKS.md`, latest `DECISIONS.md` entries
- Load the skill

End:
- All work committed and pushed on your branch
- `TASKS.md` reflects reality
- `log` entry on every task you touched
