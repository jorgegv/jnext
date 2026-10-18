// A loopback HTTP/1.1 server for the resumable-download rows (GH #358): one
// in-memory body, programmable per request. It binds 127.0.0.1 on an ephemeral
// port (never a fixed one, never a wider address), serves ONE request per
// connection, and is bounded everywhere: a request that does not arrive in 5 s
// is dropped, a send that cannot finish in 10 s is abandoned, and the thread
// itself ends after 120 s whatever happens, so a hung row cannot wedge the suite.
//
// It stands in for the NextPi mirror (nginx) and the SD distro server: strong
// ETag, Last-Modified, Range -> 206 + Content-Range, If-Range. Everything a row
// needs to break is a field of `Opts`. Every request's Range and If-Range
// headers are recorded.
//
// A dropped network (GH #358, the stall rows) is a connection that stays open
// and carries nothing: `stall_after` sends that many body bytes and then goes
// silent, `silent` answers nothing at all, and `stall_ms` is how long such a
// connection is held. The hold ends early when the client closes the connection
// or the Server is destroyed, so a row that waits builds its OWN Server: its
// destructor is what lets go of a connection the client abandoned (WinHTTP under
// wine only closes an abandoned request's socket when the server does). The
// server serves one connection at a time, so a held one is the whole server.
#pragma once

#include "esp01/esp_socket.h"
#include "esp01/esp_socket_platform.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rs {

constexpr std::size_t kNoDrop = static_cast<std::size_t>(-1);

/// The deterministic body: no randomness, every byte reproducible.
inline std::string pattern(std::size_t n, unsigned seed = 1) {
    std::string b(n, '\0');
    unsigned x = seed * 2654435761u + 12345u;
    for (std::size_t i = 0; i < n; ++i) {
        x = x * 1103515245u + 12345u;
        b[i] = static_cast<char>((x >> 16) & 0xFF);
    }
    return b;
}

struct Request {
    std::string path, range, if_range;
};

struct Opts {
    std::string body;
    std::string etag;            // verbatim incl. quotes / W/; "" = none
    std::string last_modified;   // "" = none
    bool honour_range    = true;   // false: a Range request still gets 200 + the whole body
    bool answer_416      = false;  // a Range request gets 416
    bool bad_range_start = false;  // a Range request gets 206 whose Content-Range starts at 0 (body from 0)
    bool redirect_first  = false;  // any path but /real gets 302 /real
    std::size_t drop_after = kNoDrop;   // close after this many body bytes (Content-Length still full)
    bool bad_range_total = false;       // a Range request gets a 206 whose Content-Range total is wrong
    int  fail_ranged_status = 0;        // >0: a Range request gets this status (e.g. 503) with no body
    bool close_immediately = false;     // accept, read the request, answer nothing (a dead server)
    int chunk_delay_ms = 0;             // >0: send the body in 16 KiB pieces with this pause (a slow link)
    std::size_t stall_after = kNoDrop;  // send this many body bytes, then hold the connection open sending nothing
    bool silent = false;                // read the request, send nothing at all, hold the connection open
    int stall_ms = 8000;                // how long stall_after / silent hold it (ends early: client closes, ~Server)
    int status = 0;                     // >0: answer every request with this status, Content-Length: 0, no body (GH #385)
};

class Server {
public:
    Server() {
        std::string err, warn;
        if (!esp::net::init(err)) return;
        listener_ = esp::net::open_listener(esp::ipv4(127, 0, 0, 1), 0, port_, warn, err);
        if (listener_ == esp::net::kInvalidSocket) return;
        thread_ = std::thread([this] { run(); });
    }
    ~Server() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
        if (listener_ != esp::net::kInvalidSocket) esp::net::close(listener_);
    }
    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    bool ok() const { return listener_ != esp::net::kInvalidSocket; }
    std::string url(const std::string& path = "/file") const {
        return "http://127.0.0.1:" + std::to_string(port_) + path;
    }
    void set(const Opts& o) { std::lock_guard<std::mutex> g(m_); opts_ = o; }
    std::vector<Request> requests() const { std::lock_guard<std::mutex> g(m_); return reqs_; }
    void clear_requests() { std::lock_guard<std::mutex> g(m_); reqs_.clear(); }

private:
    static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

    /// Hold `c` open, sending nothing, for up to `ms`; return early when the client closes it or the Server is being destroyed.
    void hold(esp::net::NativeSocket c, int ms) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (!stop_ && std::chrono::steady_clock::now() < until) {
            std::uint8_t b[256];
            bool eof = false, failed = false, reset = false;
            std::string err;
            esp::net::recv(c, b, sizeof(b), true, eof, failed, reset, err);
            if (eof || failed) return;
            sleep_ms(5);
        }
    }

    void run() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (!stop_ && std::chrono::steady_clock::now() < deadline) {
            esp::IpAddress peer;
            std::uint16_t peer_port = 0;
            bool failed = false;
            std::string err;
            esp::net::NativeSocket c = esp::net::accept_nonblocking(listener_, peer, peer_port, failed, err);
            if (c == esp::net::kInvalidSocket) {
                if (failed) return;
                sleep_ms(2);
                continue;
            }
            serve(c);
            esp::net::close(c);
        }
    }

    static std::string header_value(const std::string& req, const char* lower_name) {
        std::size_t pos = 0;
        while (pos < req.size()) {
            std::size_t eol = req.find("\r\n", pos);
            if (eol == std::string::npos) eol = req.size();
            const std::string line = req.substr(pos, eol - pos);
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string n = line.substr(0, colon);
                for (char& ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (n == lower_name) {
                    std::size_t b = line.find_first_not_of(" \t", colon + 1);
                    return b == std::string::npos ? std::string() : line.substr(b);
                }
            }
            pos = eol + 2;
        }
        return {};
    }

    bool send_all(esp::net::NativeSocket c, const std::string& data) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        std::size_t done = 0;
        while (done < data.size() && !stop_) {
            bool failed = false;
            std::string err;
            done += esp::net::send(c, reinterpret_cast<const std::uint8_t*>(data.data()) + done,
                                   data.size() - done, failed, err);
            if (failed) return false;
            if (done < data.size()) {
                if (std::chrono::steady_clock::now() > deadline) return false;
                sleep_ms(1);
            }
        }
        return done == data.size();
    }

    void serve(esp::net::NativeSocket c) {
        std::string req;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (req.find("\r\n\r\n") == std::string::npos && !stop_) {
            std::uint8_t buf[1024];
            bool eof = false, failed = false, reset = false;
            std::string err;
            const std::size_t n = esp::net::recv(c, buf, sizeof(buf), true, eof, failed, reset, err);
            req.append(reinterpret_cast<char*>(buf), n);
            if (eof || failed || std::chrono::steady_clock::now() > deadline) return;
            if (n == 0) sleep_ms(1);
        }
        Request r;
        const std::size_t sp1 = req.find(' ');
        const std::size_t sp2 = req.find(' ', sp1 + 1);
        if (sp1 != std::string::npos && sp2 != std::string::npos) r.path = req.substr(sp1 + 1, sp2 - sp1 - 1);
        r.range    = header_value(req, "range");
        r.if_range = header_value(req, "if-range");
        Opts o;
        {
            std::lock_guard<std::mutex> g(m_);
            reqs_.push_back(r);
            o = opts_;
        }

        if (o.close_immediately) return;
        if (o.silent) { hold(c, o.stall_ms); return; }
        if (o.status > 0) {
            send_all(c, "HTTP/1.1 " + std::to_string(o.status) + " Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        if (!r.range.empty() && o.fail_ranged_status > 0) {
            send_all(c, "HTTP/1.1 " + std::to_string(o.fail_ranged_status) +
                            " Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        if (o.redirect_first && r.path != "/real") {
            send_all(c, "HTTP/1.1 302 Found\r\nLocation: /real\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        const std::size_t total = o.body.size();
        std::size_t start = 0;
        bool partial = false;
        if (!r.range.empty() && o.honour_range) {
            if (o.answer_416) {
                send_all(c, "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" + std::to_string(total) +
                                "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                return;
            }
            // "bytes=N-"
            const std::size_t eq = r.range.find('=');
            const unsigned long long n = std::strtoull(r.range.c_str() + (eq == std::string::npos ? 0 : eq + 1), nullptr, 10);
            bool valid = true;   // If-Range: the representation must be unchanged
            if (!r.if_range.empty())
                valid = (!o.etag.empty() && r.if_range == o.etag) || (!o.last_modified.empty() && r.if_range == o.last_modified);
            if (valid && n < total) {
                partial = true;
                start = o.bad_range_start ? 0 : static_cast<std::size_t>(n);
            }
        }
        std::string head;
        if (partial) {
            head = "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " + std::to_string(start) + "-" +
                   std::to_string(total - 1) + "/" +
                   std::to_string(o.bad_range_total ? total + 1 : total) + "\r\n";
        } else {
            head = "HTTP/1.1 200 OK\r\n";
        }
        head += "Content-Length: " + std::to_string(total - start) + "\r\n";
        if (!o.etag.empty()) head += "ETag: " + o.etag + "\r\n";
        if (!o.last_modified.empty()) head += "Last-Modified: " + o.last_modified + "\r\n";
        head += "Accept-Ranges: bytes\r\nConnection: close\r\n\r\n";
        std::string payload = o.body.substr(start);
        if (o.drop_after != kNoDrop && o.drop_after < payload.size()) payload.resize(o.drop_after);
        if (o.stall_after != kNoDrop && o.stall_after < payload.size()) {
            if (send_all(c, head + payload.substr(0, o.stall_after))) hold(c, o.stall_ms);
            return;
        }
        if (o.chunk_delay_ms <= 0) {
            send_all(c, head + payload);
            return;
        }
        if (!send_all(c, head)) return;
        for (std::size_t at = 0; at < payload.size(); at += 16384) {
            if (!send_all(c, payload.substr(at, 16384))) return;
            sleep_ms(o.chunk_delay_ms);
        }
    }

    esp::net::NativeSocket listener_ = esp::net::kInvalidSocket;
    std::uint16_t          port_ = 0;
    std::thread            thread_;
    std::atomic<bool>      stop_{false};
    mutable std::mutex     m_;
    Opts                   opts_;
    std::vector<Request>   reqs_;
};

}  // namespace rs
