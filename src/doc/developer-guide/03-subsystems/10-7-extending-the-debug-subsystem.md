# 3.10.7 Extending the debug subsystem

Three kinds of change come up: a new frontend, a new verb on the facade, and a
new event kind. Each has a fixed path through the tree, and each path ends in
tests that a reviewer will look for.

## Adding a frontend

**1. Decide its shape.** An in-process frontend (like the Qt debugger or the
script engine) holds the `Debugger&` and calls it from its own code on the
emulation thread. A socket frontend implements `remote::Protocol` and lets a
`remote::Server` own the listener ([3.10.1](10-1-the-socket-transport.md)). Put
it in its own directory — `src/remote/<proto>/` for a protocol server — with
no toolkit dependency unless it is a GUI, so it builds in every configuration.

**2. Become a client.** `attach()` with a `ClientInfo` whose `name` is what the
log and the client lists will show. `ClientKind` is a closed enum in the frozen
`debugger.h`. The backend does not branch on it — it only logs it
(`ATTACH client N "<name>" kind=K`) — but a new frontend gets its own
enumerator, which is a change to the frozen header (see below). Decide whether
the client arms the machine. Most do; a client that must exist without arming
anything (as the Qt GUI's breakpoint owner does) attaches with
`ClientInfo::observer`.

**3. Implement a `Listener`.** All seven methods are pure virtual, so the
compiler lists them. A listener records and returns. A socket server builds
its packets later, in `Protocol::on_notify()`, which the transport calls after
the pump's drain; the Qt adapter acts on its own tick.

**4. Map the protocol onto verbs, never onto the machine.** Read the `Result`
of every verb and turn it into the protocol's error. Use the facade's own
answers instead of computing them: `SlotInfo::space` and `rom_select()` rather
than a page number turned into a `MemSpace`, `probe_execute()` rather than your
own breakpoint table, transient subscriptions rather than a hand-made step-off.
Keep the protocol's model — slot tables, sequence numbers, bank bytes, register
packing — in your directory.

**5. Detach on every way out.** A session end, a protocol close, a dropped
socket: all of them call `detach()`, which removes everything the client owns
and releases or hands on its pause. A frontend that can crash and leave a
subscription behind is a frontend that can leave the machine hung.

**6. Wire it into the loop owners.** For a socket server: a `--<proto>-port`
option in the `src/core/cli_options.h` table and in `doc/man/jnext.1.md`
(`make cli-check` diffs the two both ways), a port in `EmulatorConfig`, and a
block in `DebugServers::start()` that opens the server and calls
`add_service()`. The three loop owners already call `DebugServers::start()`, so
nothing else in `src/platform/` or `src/gui/` changes. `--debug-listen-address`
is refused without a server port; add yours to that check in `main.cpp`.

**7. Tests.**

- A unit suite under `test/remote/` (or beside the frontend) that runs the
  production `Server` over `FakeListener` / `FakeTransport` / `FakePeer`, on a
  real `Emulator` and `Debugger`, driven through `pump()`. Assert the bytes on
  the wire *and* the machine state. Register it in the test `CMakeLists.txt`,
  declare it with its exact row count in `test/unit-tests.conf` under
  `# gate: none` so it runs in both `make unit-test` and `make unit-test-sdl`,
  and give every row a literal, globally unique ID
  ([4.2](../04-testing/02-declared-suites-and-pinned-counts.md)).
- Regression rows under `test/00regression/scripts/`, declared in
  `functional_tests.conf`, that start the real binary with the port set to 0,
  read the bound port from the `listening on` log line, and drive it with an
  independently written client — ideally the protocol's own client, otherwise a
  small peer script in `test/00regression/`. Cover all three frontends (CLI
  headless, SDL, Qt); see the `dzrp-*-func`, `gdb-*-func` and `zrcp-*-func` rows.

**8. Document it.** A page in this section, a row in the capability matrix of
[3.10](10-0-the-debugger-frontends.md), and a user-guide page under chapter 6.

## Adding a verb to the facade

The four published headers are frozen. A new declaration is made only by owner
decision, and recorded in three places: at the declaration itself, in
`doc/design/debug-subsystem/b0-cap-traceability.md` under its capability id, and
in `doc/design/debug-subsystem/backend.md`. Every addition so far
(`on_cold_boot_begin()`, `flush_captures()`, `rom_select()`, `rgb333_to_argb()`)
followed that path. The rules the verb has to follow:

- **Return type.** `Result` if it acts, `Expected<T>` if it acts and yields
  data, a plain value only if it is a query that cannot refuse — and then it
  goes on `debugger.h`'s "DIRECT-VALUE QUERIES" list, with the bucket count
  updated.
- **Attribution.** `ClientId by` first if it transitions or mutates anything.
- **Deliveries.** If it executes, rewinds, restores, resets or replaces the
  machine, or changes its run state, it starts with
  `impl_->refuse_inside_delivery("<verb>")`.
- **Mutation.** If it writes, it emits a `MUTATE` line through `log_mutate()` or
  `log_mutate_range()`, refuses with `RefusedRzx` while an RZX records or plays,
  and runs its write under `DebugState::InspectionScope` so it fires no event.
- **State.** New state goes into `Debugger::Impl`, never into the header. If it
  must survive a hard reset and it lives on the `Emulator`, record the request
  on `Impl` and re-apply it in `reapply_after_machine_rebuild()`.
- **Includes.** A published header may not reach `Emulator` or the other
  forbidden headers: `test/lint-debug-headers.sh` (regression preflight row 5)
  fails the run if it does. A new enum that mirrors an internal one gets a
  value-by-value `static_assert` in `debug_types_check.cpp`.

Its tests are `debugger_backend_test` rows under the capability id: a wiring row
that proves the verb does what it promises, and a control row that proves the
same program does something else without it.

## Adding an event kind

The backend comes first; the frontends follow.

**1. `src/debug/events.h`.** Add the enumerator to `EventKind` immediately
before `Count`, so the count and every mask derived from it move.
`EventKindMask` is 32 bits wide. Add the `EventFilter` fields its cheap filter
needs and the `Event` payload fields its subscribers need; `Event` is one flat,
trivially copyable struct, so a payload field is a plain member. Document at the
enumerator where the kind is latched and when it is delivered, including
whether it is late.

**2. `src/debug/debug_types_check.cpp`.** Update the assertions that pin the last
kind's bit and the count.

**3. `src/debug/event_table.*`.** Add the kind's arm to
`EventTable::filter_matches()`. If the latch needs fields `LatchEntry` does not
have, add them there.

**4. The latch site, in the core.** Gate the site so a run with no subscriber
pays nothing:

- a site that fires at most a few times a frame tests the table's kind bit
  (`has_kind()`), as the `Frame`, `IntAck`, `Nmi`, `Reset` and `Magic` sites do;
- a hot site carries a plain bool on its own subsystem, set by
  `Debugger::Impl::gates_changed()`, as the Copper and DMA sites do — one bool
  per sub-kind if it has sub-kinds.

Then fill a `LatchEntry` and call `DebugState::latch_event()`. The stamper the
`Emulator` installs fills the common header (cycle, frame, `vc`, `hc`, PC), and
the ring takes care of overflow. A site in the device cluster
(`tick_devices_after_instruction()`) is delivered one instruction late; say so.

**5. `src/debug/debugger_events.cpp`.** Add the kind's arms to `build_event()`
(latch entry to `Event`) and `log_line_for()` (its `Log` action's line). Decide
what a `Stop` on it reports in `note_event_stop()`: the default is
`PauseReason::Kind::Script`.

`jnext_debug` and `jnext_script` are built with `-Werror=switch`, so a switch
without a `default` that misses the new kind fails the build. Not every switch
is like that: the Qt `BreakpointModel`'s kind-name switch has a `default`, and
`src/remote/` is not built with the flag, so grep for `EventKind::Dma` and visit
every hit.

**6. Tests.** `debugger_backend_test` gets an `EVT-<KIND>-*` family: the filter
on both sides of every bound, the delivery point (which boundary, and the
payload's PC), `once`, a `Stop` and its `pause_reason`, and a control row with
no subscriber. If the site is hot, add it to the `make bench-hotlatch` A/B or
measure it the same way. Update the suite's count in `test/unit-tests.conf`.

**7. The frontends.** The script language is the frontend that reaches every
kind; its steps — the keyword, the `EventType`, the parser, the payload table,
the engine's subscription and the `SCRIPT-EV` rows — are in
[3.10.6](13-the-debugger-scripting-language.md#adding-an-event-kind). The Qt
breakpoint list needs a name for the kind in `breakpoint_model.cpp`, and the
GDB server's `monitor` listing one in `rsp_server.cpp`.
