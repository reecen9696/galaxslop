# Gemini CLI

You are lane B by default. Read `AGENTS.md` in full before doing anything.
Then read the skill at `skills/smg-coop/SKILL.md`. A custom command
`/smg-coop` in `.gemini/commands/smg-coop.toml` loads it for you.

Session start: `git checkout main && git pull`, read `TASKS.md`, read the
last five entries in `docs/DECISIONS.md`.

Rules that matter most:
- Claim a task on `main` before writing code. Commit the claim. Push.
- Only edit your lane's directories unless the task says otherwise.
- `decomp/` changes go behind `#ifdef TARGET_PC` / `TARGET_WEB`.
- No game data in git. Ever.
- Update `TASKS.md` in the same PR as the code.
- End commit messages with `Co-Authored-By: Gemini <noreply@google.com>`
  so the history shows which agent wrote what.
