// jnext's security + visibility layer for the emulated ESP-01. Rationale is in
// the header; this file only implements it.

#include "peripheral/esp_host_policy.h"

#include "core/log.h"

#include <algorithm>
#include <cctype>
#include <utility>

std::string esp_normalise_host(const std::string& host) {
    std::size_t begin = 0;
    std::size_t end   = host.size();
    auto is_space     = [](unsigned char c) { return std::isspace(c) != 0; };
    while (begin < end && is_space(static_cast<unsigned char>(host[begin]))) ++begin;
    while (end > begin && is_space(static_cast<unsigned char>(host[end - 1]))) --end;

    std::string out = host.substr(begin, end - begin);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool EspHostPolicy::allows(const std::string& host) const {
    if (allowed_hosts.empty()) return true;   // see the header: NOT deny-all
    const std::string want = esp_normalise_host(host);
    if (want.empty()) return false;           // an unnamed host matches nothing
    for (const std::string& entry : allowed_hosts) {
        if (esp_normalise_host(entry) == want) return true;
    }
    return false;
}

bool EspHostPolicy::add(const std::string& host) {
    const std::string normalised = esp_normalise_host(host);
    if (normalised.empty()) return false;
    // Compare normalised, store as written: the list is shown back to the user
    // in log lines and in the GUI, and echoing their own spelling is what makes
    // "is my entry in there?" answerable at a glance.
    for (const std::string& entry : allowed_hosts) {
        if (esp_normalise_host(entry) == normalised) return true;
    }
    allowed_hosts.push_back(host);
    return true;
}

const char* esp_event_text(EspEvent::Kind kind) {
    switch (kind) {
        case EspEvent::Kind::Refused:        return "refused";
        case EspEvent::Kind::Opened:         return "opened";
        case EspEvent::Kind::Failed:         return "failed";
        case EspEvent::Kind::Closed:         return "closed";
        case EspEvent::Kind::TransportFault: return "transport fault";
    }
    return "unknown";
}

void EspConnectionLog::push(EspEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(std::move(event));
    // Drop-oldest: the newest event is the one a status bar shows, so losing
    // the tail of a long-finished history is the harmless direction.
    while (events_.size() > CAPACITY) events_.pop_front();
    ++sequence_;
}

std::vector<EspEvent> EspConnectionLog::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<EspEvent>(events_.begin(), events_.end());
}

std::uint64_t EspConnectionLog::sequence() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sequence_;
}

EspGatedTransport::EspGatedTransport(std::unique_ptr<esp::EspTransport> inner,
                                     EspHostPolicy policy, EspConnectionLog& log)
    : inner_(std::move(inner)), policy_(std::move(policy)), log_(log) {}

bool EspGatedTransport::begin_connect(const std::string& host, std::uint16_t port) {
    if (!policy_.allows(host)) {
        ++refusals_;
        host_ = host;
        port_ = port;
        // Straight to jnext's logger rather than through the module's logging
        // seam: this line must survive the seam's own threshold, which exists
        // only to save formatting work and is not a policy knob for a
        // security event. It stays a `warn` on a logger whose default level is
        // `info`, so it is emitted unless the user explicitly silences esp01.
        Log::esp01()->warn(
            "REFUSED connection to {}:{} — host is not in the --esp-allow list",
            host, port);
        log_.push({EspEvent::Kind::Refused, host, port, "not in the allowlist"});
        // "Request rejected", per the interface contract — the engine turns
        // this into ERROR, which every evidenced client handles.
        return false;
    }

    const bool accepted = inner_->begin_connect(host, port);
    if (accepted) {
        host_ = host;
        port_ = port;
    }
    note_state();
    return accepted;
}

void EspGatedTransport::poll() {
    inner_->poll();
    note_state();
}

esp::TransportState EspGatedTransport::state() const { return inner_->state(); }

const std::string& EspGatedTransport::last_error() const { return inner_->last_error(); }

const esp::IpAddress& EspGatedTransport::peer_address() const {
    return inner_->peer_address();
}

std::size_t EspGatedTransport::send(const std::uint8_t* data, std::size_t len) {
    const std::size_t sent = inner_->send(data, len);
    note_state();
    return sent;
}

std::size_t EspGatedTransport::recv(std::uint8_t* buf, std::size_t cap) {
    const std::size_t got = inner_->recv(buf, cap);
    note_state();
    return got;
}

void EspGatedTransport::close() {
    inner_->close();
    note_state();
}

// NOTHING HERE LOGS, and that is deliberate. `AtEngine` already emits
// "connection to H:P opened / failed / closed by the guest / closed by the
// peer" at info/warn, on by default, for every one of these transitions
// (esp_at.cpp). Re-logging them from the decorator would print each
// security-relevant event twice, which makes the log LESS trustworthy, not
// more. The gap this branch had to close was never the stderr line — it was
// that stderr is invisible in the GUI. So this function's whole job is to
// mirror those events into a place the GUI can read.
//
// The refusal in `begin_connect` is the exception, and logs, because the
// engine's own refusal line ("refused before it started") cannot say WHY —
// only this layer knows the allowlist exists.
void EspGatedTransport::note_state() {
    const esp::TransportState now = inner_->state();
    if (now == last_state_) return;
    const esp::TransportState was = last_state_;
    last_state_ = now;

    switch (now) {
        case esp::TransportState::Connected:
            log_.push({EspEvent::Kind::Opened, host_, port_,
                       esp::to_string(inner_->peer_address())});
            break;
        case esp::TransportState::Failed:
            log_.push({EspEvent::Kind::Failed, host_, port_, inner_->last_error()});
            break;
        case esp::TransportState::Closed:
            // Only report a close of something that was actually up. An attempt
            // that never connected would otherwise report a connection the user
            // never had.
            if (was == esp::TransportState::Connected)
                log_.push({EspEvent::Kind::Closed, host_, port_, {}});
            break;
        case esp::TransportState::Idle:
        case esp::TransportState::Resolving:
        case esp::TransportState::Connecting:
            break;
    }
}

bool esp_note_transport_fault(std::uint64_t pass_exceptions, bool& already_reported) {
    if (pass_exceptions == 0) return false;
    if (already_reported) return false;
    already_reported = true;
    return true;
}
