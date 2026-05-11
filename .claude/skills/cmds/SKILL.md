---
name: cmds
description: List all project-local skills, subagents, and reference docs for the jnext repo with one-line descriptions. Use when the user says "what skills do I have", "list commands", "what's available", "what can you do here", or asks to discover the project's .claude/ inventory.
---

# Discover available skills, subagents, and docs

List everything under `.claude/` for this project so the user knows what's available.

## Steps

Run this in a single Bash call (parse frontmatter `description:` field with awk; fall back to "(no description)"):

```bash
ROOT=/home/jorgegv/src/spectrum/jnext/.claude

emit() {
  local file="$1" name="$2"
  local desc
  desc=$(awk -F': ' '/^description:/{sub(/^description:[[:space:]]*/, "", $0); print; exit}' "$file" 2>/dev/null)
  [ -z "$desc" ] && desc="(no description)"
  printf "  %s — %s\n" "$name" "$desc"
}

echo "## Skills (in .claude/skills/ — auto-invoked when user intent matches)"
for f in "$ROOT"/skills/*/SKILL.md; do
  [ -f "$f" ] || continue
  name=$(basename "$(dirname "$f")")
  emit "$f" "$name"
done

echo ""
echo "## Subagents (in .claude/agents/ — dispatched via Agent tool with subagent_type=NAME)"
for f in "$ROOT"/agents/*.md; do
  [ -f "$f" ] || continue
  name=$(basename "$f" .md)
  emit "$f" "$name"
done

echo ""
echo "## Reference docs (in .claude/docs/ — read for project conventions)"
for f in "$ROOT"/docs/*.md; do
  [ -f "$f" ] || continue
  name=$(basename "$f")
  first_para=$(awk 'NR>1 && NF{print; exit}' "$f" 2>/dev/null | head -c 120)
  [ -z "$first_para" ] && first_para="(see file)"
  printf "  %s — %s\n" "$name" "$first_para"
done

# Note: project no longer has .claude/commands/ — everything is a skill so
# auto-invocation works. The list above is exhaustive.
```

After printing, briefly note:

- **Skills** are auto-invoked when your message matches their "Use when..." trigger.
- **Subagents** are dispatched via the `Agent` tool when work matches their scope; you don't type them.
- **Reference docs** are background reading; I open them when relevant.
