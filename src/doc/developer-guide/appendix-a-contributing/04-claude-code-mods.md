# A.4 Claude Code mods

The repository ships two optional Claude Code mods under `.claude/mods/`. They
are for people who drive the project with Claude Code, in particular with its
autonomous run; nothing in the build, the tests or the emulator depends on them,
and a contributor who does not use Claude Code can ignore this page.

## What they do

- **`conv-logs`** keeps the conversation to what you asked. Messages that arrive
  unrequested (a background task's notification, another agent's hand-back) and
  the turn Claude runs in answer to them move to a Logs pane beside the chat.
  A log entry for one of Claude's replies links back to the place in the
  conversation it came from.
  `/logs` opens the pane; `/logs-split` turns the split on or off.
- **`run-status`** shows a Run status pane for an autonomous run, refreshed every
  5 minutes and on demand by `/run-status`. It makes no model calls. It reads the
  run directory's `agents.md` and `blocked.md`, `gh` for the milestone's open
  issues and the CI runs, `git` for `main` and the epic branches, and the
  remote-run lane probe, and shows who is blocked on whom, what runs, what comes
  next and the state of the remote hosts.

`run-status` has no settings to edit. It derives them when it refreshes: the home
directory from the environment, the repository root and the project name from
git, the run directory as the newest dated directory under
`~/tmp/autorun/<project>/`, the remote hosts and their work directory from that
run's `slot.sh`, the milestone as the lowest version among the open ones (read
from the title, so `v1.5` comes before `v1.10`), and the repository owner from
GitHub. The owner and the milestone are asked of GitHub once per session.
Whatever cannot be derived is shown as "not configured" in the pane.

## Installing

Each mod is listed in the marketplace file `.claude-plugin/marketplace.json` at
the repository root, and is opt-in. At the prompt of a terminal Claude Code
session:

```
/plugin install conv-logs --marketplace jorgegv/jnext
/plugin install run-status --marketplace jorgegv/jnext
```

Answer `y` to add the marketplace, then pick a scope. From a local clone, add the
clone as the marketplace instead:

```
claude plugin marketplace add <path to the clone>
claude plugin install conv-logs@jnext
claude plugin install run-status@jnext
```

A mod installed from a folder marketplace is read from the clone itself, so after
editing it `/reload-plugins` picks the change up.

## Checking a change

```
claude plugin validate .claude/mods/<name>
claude plugin test .claude/mods/<name>
```

`validate` reads the manifest and the hooks module as Claude Code will;
`test` runs the mod's `tests/*.test.tsx`. These are not part of `make unit-test`:
they need the `claude` CLI.
