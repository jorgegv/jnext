# Your own labels and comments

You can name an address yourself, and write a comment on it, from the Disassembly or the Source
tab. The names show wherever the debugger shows a name (the views, the address boxes, a script's
`@name`, GDB's `monitor sym`), and they are saved in a small project file you can keep with the
program. They are yours: loading a program, a MAP file or a source map, resetting, loading a state
and rewinding never touch them.

## Adding, renaming and removing

Right-click a line in the **Disassembly** or the **Source** tab. After the navigation entries the
menu has:

| Entry | Effect |
|---|---|
| **Add Label…** / **Rename Label…** | Asks for a name. **Rename** appears when the line already shows a label. |
| **Remove Label** | Removes *your* label of the line. Disabled when the label comes from a loaded file |
| **Add Comment…** / **Edit Comment…** | Asks for a text, several lines allowed. An empty text removes the comment |

- A label name is letters, digits and `_`, not starting with a digit; a `.` may join parts
  (`main.loop`). That is what a script's `@name` can write. A name that is already used for another
  address, by you or by a loaded file, is refused with the reason; nothing changes.
- **Rename Label…** on a label that comes from a MAP or a source map gives you a label of your own
  at that address, which shows instead of it. **Remove Label** then brings the loaded one back.
  **Remove Label** is disabled on a label that is only the loaded file's, and its tooltip says which
  file.
- In the Source tab a line that compiled to several places gives each of the three entries a
  submenu with the places (`$8002 P04`, …): the entry acts on the one you choose, mapped now or not.
  A line with no code has the entries disabled (*no code at main.c:3*).

## Which memory a label belongs to

A label belongs to an address **in one memory**: a RAM page, a ROM, the DivMMC's memory. It shows
only while that memory is the one at the address. A label on `$C000` of page `0x10` shows when page
`0x10` is mapped at `$C000` and not when page `0x11` is, exactly like a banked label of a MAP file
([Symbols from MAP files](05-symbols-from-z88dk-map-files.md)). The memory is the one that supplies the
line's address now (the fetch's, for the instruction being run), or, in the Source tab, the one the
source line compiled to.

## What the Disassembly shows

When any label is known (yours or a loaded file's), the Disassembly gets a **Label** column between
the bytes and the instruction, with the name of the line's own address, shortened with `...` after 16
characters. A comment follows the instruction in a muted colour: `; first line`, with ` ...` when it
has more. The copy commands do not copy labels or comments. The Source tab shows your own source file
and does not draw them.

## The project file

**Map ▸ Open Annotations…**, **Save Annotations** and **Save Annotations As…** (the `.jdp` suffix is
added when the name has none). Open replaces everything you have, or nothing if the file is wrong:
the message names the line. If you have unsaved changes, Open and quitting ask whether to **Save**,
**Discard** or **Cancel**; with nothing changed they do not ask.

`jnext --annotations FILE` loads a file at start-up, before any script, and Save then writes back
to it. A file that cannot be read is an error and jnext exits with status 1.

The file is text, in UTF-8:

```
jnext-annotations 1
label P04 $8000 Start
comment P04 $8000 Entry\npoint
label ROM0 $0010 rst_print
```

A line is `label MEMORY $ADDRESS NAME` or `comment MEMORY $ADDRESS TEXT`. The memory is the name the
views show (`P04` is RAM page 4, `ROM0`, `DIVROM`, `DIVRAM2`…); in a comment `\n` is a line break and
`\\` a backslash. Lines starting with `#` and blank lines are ignored when reading, but **Save does
not keep them**: put a note on an address as a comment, not as a `#` line. Saving writes the
entries sorted, so the file compares well in a repository.

If a MAP or source map loaded later uses a name you also use, yours wins.
