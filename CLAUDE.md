# Claude Code

You are lane A by default. Read `AGENTS.md` in full before doing anything.
Then load the skill: `/smg-coop` (it lives at `skills/smg-coop/SKILL.md`,
mirrored into `.claude/skills/smg-coop/`).

Session start: `git checkout main && git pull`, read `TASKS.md`, read the
last five entries in `docs/DECISIONS.md`.

Rules that matter most:
- Claim a task on `main` before writing code. Commit the claim. Push.
- Only edit your lane's directories unless the task says otherwise.
- `decomp/` changes go behind `#ifdef TARGET_PC` / `TARGET_WEB`.
- No game data in git. Ever.
- Update `TASKS.md` in the same PR as the code.
- Commits end with the attribution lines your harness adds. Keep them.
