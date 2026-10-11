// OS-level scaffolding of nextpi_test (GH #316): process liveness, a hard kill,
// a held-open child process, and a pipe to leak. Kept in a translation unit of
// its own so <windows.h> never reaches the suite's, which includes the
// emulator's headers (the rule src/core/win_process.cpp states for the product).
#pragma once
#include <string>
#include <vector>

namespace nxs {

/// True while `pid` is a live process (a zombie is not alive).
bool process_alive(unsigned long pid);

/// True once `pid` is gone, polling for at most `ms` milliseconds.
bool gone_within(unsigned long pid, int ms);

/// Kill `pid` the way a crash or Task Manager would: no destructor, no handler.
void kill_hard(unsigned long pid);

unsigned long self_pid();

/// A child process started with its stdout captured. `start` returns once the
/// child has printed a line starting `ready` or `failed` (others are skipped) or `timeout_ms` passed.
class Child {
public:
    Child() = default;
    ~Child();
    Child(const Child&)            = delete;
    Child& operator=(const Child&) = delete;
    bool start(const std::string& exe, const std::vector<std::string>& args,
               std::string& first_line, int timeout_ms);
    unsigned long pid() const { return pid_; }
    /// kill_hard() and reap.
    void kill();

private:
    unsigned long pid_ = 0;
    long          read_end_ = -1;       // fd, or a HANDLE value on Windows
    void*         process_  = nullptr;  // Windows process HANDLE
};

/// An INHERITABLE pipe that already holds the eight bytes JNEXTMAG; `number()`
/// is its read end (a file descriptor >= 10, or a HANDLE value). A child that
/// inherited it can read the magic; one that did not, cannot.
class MagicPipe {
public:
    MagicPipe();
    ~MagicPipe();
    MagicPipe(const MagicPipe&)            = delete;
    MagicPipe& operator=(const MagicPipe&) = delete;
    bool ok() const { return number_ >= 0; }
    long number() const { return number_; }

private:
    long number_ = -1;
};

} // namespace nxs
