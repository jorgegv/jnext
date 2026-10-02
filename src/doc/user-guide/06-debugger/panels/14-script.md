# Script

The debugger scripts you have loaded, what their rules are doing, and what
they have concluded. A debugger script (`.jds`) is a list of rules, each an
event and the actions to run when it happens; [Debugger
scripting](../scripting/index.md) describes the language, and [Debugger
scripts](../functions/13-debugger-scripts.md) how to load them.

![The Script tab](../../img/debugger-script.png)

At the top, three buttons:

- **Load...** — load one more script (the same as **Script > Load Script...**).
- **Reload** — load the same files again, in the same order. Hit counts start
  over and a `once` rule is armed again.
- **Unload All** — remove every script.

Under them, the **recorder**, which writes what you do into a replay script
([Recording and replaying a session](../scripting/10-recording-and-replaying.md)):

- **Record...** — choose a file and start recording (the same as
  **Script > Record Script...**).
- **Capture** — capture the screen at the next frame (so does **Alt+8** while
  recording, in either window).
- **Stop Recording** — write the script.

and a line saying what is being recorded, or what was last.

Below them, the **verdict line**: how many scripts are loaded and what the run
has reached so far.

- **PASS: exit 0** or **FAIL: exit *n*** — a rule ran `exit`. In the GUI a
  script never ends JNEXT: the machine pauses instead.
- **FAIL: *n* stop(s), the last: *reason*** — a `stop`, a failed `assert` or a
  failed `compare_scr` stopped the machine.
- **ERROR: *n* rule(s) disabled by a run-time error** — a rule hit an error
  (a division by zero, say) and was switched off. The log says where.
- ***n* verdict(s) not reached yet** — rules that hold an `exit` or a
  `compare_scr` and have not run.

Then one row per **rule**:

| Column | Shows |
|---|---|
| File | the script it comes from |
| Rule | its label, or its `line:column` |
| Event | the event with its filter as registered — symbols already turned into addresses, e.g. `write 9000..9001` |
| State | `armed`, `disabled`, `spent (once)` for a `once` rule that has fired, or `error (disabled)`; `verdict not reached` is added to a rule holding `exit` or `compare_scr` that has not run |
| Hits | how many times its body has run |

At the bottom, the **script log**: every `log` line, `dump_*` output, `SCRIPT
STOP` and `ASSERT FAILED` line and load error, and the `MUTATE` line of every
change a script made to the machine. The last 2000 lines are kept.
