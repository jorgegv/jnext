// PiAudio's Windows transport: a named-pipe SERVER that QEMU's wav back-end
// writes to. QEMU has no audio back-end that writes to a socket, only `wav`,
// which fopen()s a path; `\\.\pipe\...` is such a path on Windows. The pipe
// must therefore exist before QEMU starts (fopen of a missing pipe fails).
// The server is the configuration Wireshark's extcap code creates for a client
// that opens it with fopen("wb"): duplex, byte mode, one instance, 64 KiB.
//
// The reader never polls: it waits on the I/O event and the stop event, so it
// wakes at once for data, for the writer leaving, and for close(); with no
// writer it sleeps in ConnectNamedPipe. After a writer leaves the pipe is
// disconnected and listens again (QEMU opens its file once per run, so this is
// a robustness path; a writer that reopens inside the re-listen window gets one
// failed fopen). This TU is the only one of jnext_audio that includes
// <windows.h>, the rule src/core/win_process.cpp states.
#ifdef _WIN32

#include "audio/pi_audio.h"

#include <windows.h>

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

enum class Wait { Done, Failed, Stopped };

/// Wait for the overlapped operation `ov` (already started, pending) or for
/// `stop`. On stop the operation is cancelled and reaped, because the buffer and
/// the OVERLAPPED must outlive it. On Failed, `err` is the I/O's error.
Wait finish(HANDLE pipe, OVERLAPPED& ov, HANDLE stop, DWORD& n, DWORD& err,
            std::atomic<uint64_t>& wakeups) {
    const HANDLE h[2] = {stop, ov.hEvent};
    const DWORD w = ::WaitForMultipleObjects(2, h, FALSE, INFINITE);
    wakeups.fetch_add(1, std::memory_order_relaxed);   // every return of the wait (PiAudio::idle_reads)
    if (w == WAIT_OBJECT_0) {
        ::CancelIoEx(pipe, &ov);
        DWORD ignored = 0;
        ::GetOverlappedResult(pipe, &ov, &ignored, TRUE);
        return Wait::Stopped;
    }
    if (::GetOverlappedResult(pipe, &ov, &n, FALSE)) return Wait::Done;
    err = ::GetLastError();
    return Wait::Failed;
}

void arm(OVERLAPPED& ov, HANDLE io) {
    ov = OVERLAPPED{};
    ov.hEvent = io;
    ::ResetEvent(io);
}

} // namespace

bool PiAudio::open(const std::string& name, std::string& error) {
    close();
    HANDLE pipe = ::CreateNamedPipeW(widen(name).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                     PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                     1, 65536, 65536,
                                     0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        error = name + ": CreateNamedPipe: error " + std::to_string(::GetLastError());
        return false;
    }
    HANDLE stop = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE io   = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop || !io) {
        error = name + ": CreateEvent: error " + std::to_string(::GetLastError());
        if (stop) ::CloseHandle(stop);
        if (io) ::CloseHandle(io);
        ::CloseHandle(pipe);
        return false;
    }
    pipe_ = pipe;
    stop_event_ = stop;
    io_event_ = io;
    thread_ = std::thread([this] { run(); });
    return true;
}

void PiAudio::close() {
    if (stop_event_) ::SetEvent(static_cast<HANDLE>(stop_event_));
    if (thread_.joinable()) thread_.join();
    if (pipe_) { ::CloseHandle(static_cast<HANDLE>(pipe_)); pipe_ = nullptr; }
    if (stop_event_) { ::CloseHandle(static_cast<HANDLE>(stop_event_)); stop_event_ = nullptr; }
    if (io_event_) { ::CloseHandle(static_cast<HANDLE>(io_event_)); io_event_ = nullptr; }
}

void PiAudio::run() {
    HANDLE pipe = static_cast<HANDLE>(pipe_);
    HANDLE stop = static_cast<HANDLE>(stop_event_);
    HANDLE io   = static_cast<HANDLE>(io_event_);
    OVERLAPPED ov{};
    Stream st;
    std::vector<uint8_t> buf(1 << 16);
    for (;;) {
        // Listen for the writer.
        arm(ov, io);
        if (!::ConnectNamedPipe(pipe, &ov)) {
            const DWORD e = ::GetLastError();
            if (e == ERROR_IO_PENDING) {
                DWORD n = 0, err = 0;
                const Wait w = finish(pipe, ov, stop, n, err, idle_reads_);
                if (w == Wait::Stopped) return;
                if (w == Wait::Failed) {
                    ::DisconnectNamedPipe(pipe);
                    if (::WaitForSingleObject(stop, 100) == WAIT_OBJECT_0) return;   // never spin
                    continue;
                }
            } else if (e == ERROR_NO_DATA) {          // a writer came and went already
                ::DisconnectNamedPipe(pipe);
                continue;
            } else if (e != ERROR_PIPE_CONNECTED) {
                if (::WaitForSingleObject(stop, 100) == WAIT_OBJECT_0) return;
                continue;
            }
        }
        // Read until the writer goes.
        for (;;) {
            arm(ov, io);
            DWORD got = 0;
            bool ok = ::ReadFile(pipe, buf.data(), static_cast<DWORD>(buf.size()), &got, &ov) != 0;
            if (!ok) {
                if (::GetLastError() != ERROR_IO_PENDING) {
                    got = 0;
                } else {
                    DWORD err = 0;
                    const Wait w = finish(pipe, ov, stop, got, err, idle_reads_);
                    if (w == Wait::Stopped) return;
                    ok = w == Wait::Done;
                }
            }
            if (!ok) break;                            // broken pipe: the writer is gone
            if (got > 0) consume(buf.data(), got, st);
        }
        writer_gone(st);
        eof_reads_.fetch_add(1, std::memory_order_relaxed);
        ::DisconnectNamedPipe(pipe);
    }
}

#endif // _WIN32
