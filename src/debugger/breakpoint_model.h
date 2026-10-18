#pragma once

#include <QObject>
#include <QString>

#include <cstdint>
#include <vector>

#include "debug/debugger.h"

/// GH #278 WP4c — the Qt GUI's breakpoints, as BACKEND SUBSCRIPTIONS.
///
/// Until WP4c the GUI's breakpoints lived in the core's `BreakpointSet`, which
/// the Breakpoints panel and the disassembly gutter observed. They are now
/// `jnext::dbg::Debugger` subscriptions (REQ-qt-13d): single-address
/// `Execute`, `Mem` and `Port` subscriptions with no condition, `once = false`
/// and the `Stop` action, owned by this model's client. READ_WRITE is ONE
/// subscription with an access mask (REQ-qt-13c). The backend's table is the
/// only model; this class holds no breakpoint of its own.
///
/// THE OWNER IS A NON-ARMING OBSERVER CLIENT (REQ-qt-32, `ClientInfo::observer`),
/// attached for this object's lifetime. That is what lets the user's
/// breakpoints outlive the debugger window — a closed window keeps them, and
/// with `--persistent-breakpoints` they fire with the window shut (GH #219) —
/// while a closed window still leaves the machine UNARMED (PBPUI-03): the
/// window's own client, attached only while it is open, is what arms it
/// (qt-frontend.md §4.1, WP4c). A breakpoint therefore fires exactly when
/// something arms the machine, as a `BreakpointSet` entry did.
///
/// NOTIFICATION — the successor of the `BreakpointSet` observers (GH #220):
/// `changed(kinds)` is emitted with the kinds whose LISTED state changed —
/// `Execute` for the gutter, `Mem` / `Port` for the list only (REQ-qt-13b).
/// Two routes feed it, and both compute the kinds the same way, by diffing the
/// backend's listing against the one last published:
///   * every mutator of this class, SYNCHRONOUSLY, before it returns — so no
///     GUI route can forget to repaint, which is GH #220's whole point;
///   * the backend's `SubscriptionsChanged` push, for a change another client
///     made. The listener only RECORDS it (REQ-qt-15b: no UI work inside a
///     backend callback); `sync()`, called once per tick after the pump,
///     publishes it. The push that echoes this class's own change finds
///     nothing new and emits nothing.
/// A transient subscription (Step Over, Run to Here) is not listed, so its
/// coming and going changes nothing drawn and is not notified (GH #220-05).
class BreakpointModel : public QObject, private jnext::dbg::Listener {
    Q_OBJECT
public:
    /// The Add dialog's six types, in its combo order.
    enum Type { Execute = 0, Read, Write, ReadWrite, IoRead, IoWrite };

    /// One LISTED subscription, whoever owns it (REQ-qt-13d).
    struct Row {
        jnext::dbg::EventId id = jnext::dbg::EVENT_NONE;
        /// A `Type`, or -1 for a subscription the GUI cannot express — some
        /// other client's range, NextREG, frame... watch.
        int      type    = -1;
        uint16_t addr    = 0;   ///< the address, the port, or a range's low end
        /// An Execute row's physical 8K page qualifier (CAP-SRC: a breakpoint
        /// set on a source line of banked code), or `PAGE_ANY` for a logical
        /// breakpoint that matches whatever page is mapped.
        uint16_t page    = jnext::dbg::PAGE_ANY;
        bool     enabled = true;
        bool     live    = true;
        /// This GUI's own row: editable. Another client's is read-only.
        bool     own     = false;
        QString  type_text;     ///< the Type column
        QString  addr_text;     ///< the Address column
    };

    /// Attaches the observer client to `dbg`, which must outlive this object.
    explicit BreakpointModel(jnext::dbg::Debugger& dbg, QObject* parent = nullptr);
    ~BreakpointModel() override;

    /// Every listed subscription, sorted by address; at one address this GUI's
    /// Execute row comes first, then the rest in creation order.
    const std::vector<Row>& rows() const { return rows_; }

    /// Create a breakpoint, ENABLED. Idempotent: one that already exists keeps
    /// its own enabled flag (GH #225 — a second gutter click must not re-arm a
    /// breakpoint the user disabled).
    ///
    /// `page` qualifies an Execute breakpoint with a physical 8K page
    /// (CAP-SRC); every other type, and the default, is unqualified. A
    /// qualified and an unqualified breakpoint at one address are two rows.
    void add(int type, uint16_t addr, uint16_t page = jnext::dbg::PAGE_ANY);
    void remove(int type, uint16_t addr, uint16_t page = jnext::dbg::PAGE_ANY);
    /// No-op if there is no such breakpoint or the flag already has that value.
    void set_enabled(int type, uint16_t addr, bool enabled,
                     uint16_t page = jnext::dbg::PAGE_ANY);
    /// Remove every breakpoint THIS GUI owns. Other clients' are not the GUI's
    /// to delete.
    void clear_all();

    bool exists(int type, uint16_t addr, uint16_t page = jnext::dbg::PAGE_ANY) const;
    /// This GUI's Execute breakpoint at `addr`: is there one, and can it fire.
    bool pc_exists(uint16_t addr) const;
    bool pc_live(uint16_t addr) const;
    /// What the gutter draws at `addr` while physical `page` is mapped there:
    /// the logical breakpoint, or one qualified with that page (CAP-SRC).
    bool pc_marked(uint16_t addr, uint16_t page) const;
    bool pc_marked_live(uint16_t addr, uint16_t page) const;

    /// GH #225's master switch — the backend's, one for every client.
    bool master_enabled() const;
    void set_master_enabled(bool enabled);

    /// The observer client that owns the GUI's breakpoints.
    jnext::dbg::ClientId client() const { return client_; }

    /// Once per tick, after the loop owner's pump: publish a change another
    /// client made, if the backend pushed one.
    void sync();

signals:
    /// The listed state changed; `kinds` is a `jnext::dbg::EventKindMask` of
    /// the kinds whose rows changed (all three for a master-switch flip).
    void changed(uint32_t kinds);

private:
    /// Re-read the backend's listing, rebuild rows_, and emit changed() with
    /// the kinds that differ from the last published state.
    void publish_();
    const Row* find_own_(int type, uint16_t addr,
                         uint16_t page = jnext::dbg::PAGE_ANY) const;

    // jnext::dbg::Listener — only the subscription push is used; the pause
    // state is PULLED by DebuggerManager (§4.1), so the rest are deliberately
    // empty.
    void on_paused(const jnext::dbg::PausedInfo&) override {}
    void on_resumed(jnext::dbg::ClientId) override {}
    void on_reset(jnext::dbg::ResetKind) override {}
    void on_frame_ended(uint32_t) override {}
    void on_subscriptions_changed(jnext::dbg::EventKindMask) override { dirty_ = true; }
    void on_exit_requested(int) override {}
    void on_log(jnext::dbg::LogLevel, const std::string&) override {}

    jnext::dbg::Debugger& dbg_;
    jnext::dbg::ClientId  client_ = jnext::dbg::CLIENT_NONE;
    std::vector<Row>      rows_;
    /// What the last publish saw, per listed id: its kind and whether it was
    /// enabled / live. A row's filter cannot change without a new id.
    struct Seen {
        jnext::dbg::EventId   id;
        jnext::dbg::EventKind kind;
        bool                  enabled;
        bool                  live;
    };
    std::vector<Seen> seen_;
    bool master_seen_ = true;
    bool dirty_       = false;
};
