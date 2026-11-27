# conv-logs

A Claude Code mod that keeps the conversation to what you asked. Everything that
arrives unrequested (a background task's notification, another agent's hand-back)
and the turn Claude runs in answer to it moves to a Logs pane beside the chat. A log
entry for one of Claude's replies links to the place in the conversation it came
from, and a reply that
names an issue links to the entries about that issue.

## Commands

- `/logs`: open the Logs pane.
- `/logs-split`: turn the split on or off (off shows everything in the conversation).

## Install

At the prompt of a terminal session:

```
/plugin install conv-logs --marketplace jorgegv/jnext
```

Answer `y` to add the marketplace, then pick a scope. From a local clone:

```
claude plugin marketplace add <path to the clone>
claude plugin install conv-logs@jnext
```

## Develop

```
claude plugin validate .claude/mods/conv-logs
claude plugin test .claude/mods/conv-logs
```
