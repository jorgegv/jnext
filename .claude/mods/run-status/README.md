# run-status

A Claude Code mod that shows a Run status pane for an autonomous run, refreshed
every 5 minutes and on demand. It never calls the model: it reads the run's files
(`agents.md`, `blocked.md`, `slot.sh`, the admissions log), `gh` (open issues,
CI runs), `git` and the remote-run lane probe, and lays them out as Blocked on
you, Running, Next, Waiting, Epic, main and CI, and Remotes.

Nothing is configured by hand. It derives, at run time: the home directory from
`$HOME`; the project name from the repository directory; the run directory (the
newest `YYYY-MM-DD` under `~/tmp/autorun/<project>/`); the remote hosts and work
dir from that run's `slot.sh` (`JNEXT_REMOTE_RUN_HOSTS` or `JNEXT_REMOTE_RUN_HOST`,
and `JNEXT_REMOTE_RUN_DIR`); the milestone (the lowest version among the open ones); and the
repository owner from `gh repo view`. A piece that cannot be derived shows as
"not configured" in the pane.

## Commands

- `/run-status`: open the pane and refresh it now.

## Install

At the prompt of a terminal session:

```
/plugin install run-status --marketplace jorgegv/jnext
```

Answer `y` to add the marketplace, then pick a scope. From a local clone:

```
claude plugin marketplace add <path to the clone>
claude plugin install run-status@jnext
```

## Develop

```
claude plugin validate .claude/mods/run-status
claude plugin test .claude/mods/run-status
```
