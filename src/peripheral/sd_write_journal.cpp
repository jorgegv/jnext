#include "peripheral/sd_write_journal.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace sd_write_journal {

namespace {

namespace fs = std::filesystem;

const char kMagic[8] = {'J', 'N', 'E', 'X', 'T', 'S', 'D', 'W'};

std::mutex g_mu;
std::map<std::string, std::shared_ptr<Journal>> g_registry;
PersistHook g_hook = nullptr;

std::string key_of(const std::string& path) {
    std::error_code ec;
    const fs::path p = fs::weakly_canonical(path, ec);
    return ec ? path : p.string();
}

void put_le(std::vector<uint8_t>& v, uint64_t x, int n) {
    for (int i = 0; i < n; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
uint64_t get_le(const uint8_t* p, int n) {
    uint64_t x = 0;
    for (int i = 0; i < n; ++i) x |= static_cast<uint64_t>(p[i]) << (8 * i);
    return x;
}

std::vector<uint8_t> header_bytes(const Binding& b) {
    std::vector<uint8_t> h(kMagic, kMagic + 8);
    put_le(h, kFormatVersion, 4);
    put_le(h, kSectorBytes, 4);
    put_le(h, b.bytes, 8);
    put_le(h, static_cast<uint64_t>(b.mtime), 8);
    h.insert(h.end(), b.sha256.begin(), b.sha256.end());
    return h;
}

std::string hex(const std::array<uint8_t, 32>& d) {
    static const char* x = "0123456789abcdef";
    std::string s;
    for (uint8_t c : d) { s += x[c >> 4]; s += x[c & 15]; }
    return s;
}

std::string describe(const Binding& b) {
    if (b.by_digest()) return "SHA-256 " + hex(b.sha256);
    return "size " + std::to_string(b.bytes) + ", mtime " + std::to_string(b.mtime);
}

bool same_image(const Binding& a, const Binding& b) {
    if (a.by_digest() != b.by_digest()) return false;
    if (a.bytes != b.bytes) return false;
    return a.by_digest() ? a.sha256 == b.sha256 : a.mtime == b.mtime;
}

bool write_whole(const std::string& file, const std::vector<uint8_t>& data) {
    const std::string tmp = file + ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        o.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
        o.flush();
        if (!o.good()) {
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

std::shared_ptr<spdlog::logger>& jlog() {
    static auto logger = []() {
        auto existing = spdlog::get("sdcard");
        if (existing) return existing;
        auto l = spdlog::stderr_color_mt("sdcard");
        l->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
        return l;
    }();
    return logger;
}

PersistHook hook_now() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_hook;
}

}  // namespace

bool Journal::lookup(uint32_t sector, uint8_t out[kSectorBytes]) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(sector);
    if (it == map_.end()) return false;
    std::memcpy(out, it->second.data(), kSectorBytes);
    return true;
}

bool Journal::empty() const {
    std::lock_guard<std::mutex> lk(mu_);
    return map_.empty();
}

bool Journal::commit(uint32_t sector, const uint8_t in[kSectorBytes]) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<uint8_t> rec;
        if (file_len_ == 0) rec = header_bytes(binding_);
        put_le(rec, sector, 4);
        rec.insert(rec.end(), in, in + kSectorBytes);

        std::error_code ec;
        const fs::path parent = fs::path(file_).parent_path();
        if (!parent.empty()) fs::create_directories(parent, ec);
        bool ok = false;
        {
            std::ofstream o(file_, std::ios::binary | std::ios::app);
            if (o.is_open()) {
                o.write(reinterpret_cast<const char*>(rec.data()),
                        static_cast<std::streamsize>(rec.size()));
                o.flush();
                ok = o.good();
            }
        }
        if (!ok) {
            // Back to the last whole record: a torn tail must not stay behind.
            std::error_code rc;
            fs::resize_file(file_, file_len_, rc);
            jlog()->warn("SD write journal: host write to {} failed; the sector is "
                                  "not kept", file_);
            return false;
        }
        file_len_ += rec.size();
        std::array<uint8_t, kSectorBytes> a;
        std::memcpy(a.data(), in, kSectorBytes);
        map_[sector] = a;
    }
    if (PersistHook h = hook_now()) h();
    return true;
}

void Journal::clear() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        map_.clear();
        file_len_ = 0;
        std::error_code ec;
        fs::remove(file_, ec);
    }
    if (PersistHook h = hook_now()) h();
}

bool bind(const std::string& image_path, const std::string& file,
          const Binding& binding, bool reset, std::string& why) {
    auto j = std::make_shared<Journal>(file, binding);

    std::error_code ec;
    const bool exists = fs::exists(file, ec);
    std::vector<uint8_t> raw;
    if (exists) {
        std::ifstream in(file, std::ios::binary);
        if (!in.is_open()) {
            why = "cannot read " + file;
            return false;
        }
        raw.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    if (!raw.empty()) {
        if (raw.size() < kHeaderBytes || std::memcmp(raw.data(), kMagic, 8) != 0 ||
            get_le(raw.data() + 8, 4) != kFormatVersion ||
            get_le(raw.data() + 12, 4) != kSectorBytes) {
            why = file + " is not a jnext SD-writes file; not touching it";
            return false;
        }
        Binding jb;
        jb.bytes = get_le(raw.data() + 16, 8);
        jb.mtime = static_cast<int64_t>(get_le(raw.data() + 24, 8));
        std::memcpy(jb.sha256.data(), raw.data() + 32, 32);

        if (reset) {
            fs::remove(file, ec);
            raw.clear();
        } else if (!same_image(jb, binding)) {
            why = file + " was made for another image (it holds writes for " + describe(jb) +
                  "; this image is " + describe(binding) + "); run with "
                  "--sdcard-writes-reset to discard them";
            return false;
        }
    }

    if (!raw.empty()) {
        const size_t body = raw.size() - kHeaderBytes;
        const size_t whole = body / kRecordBytes;
        const bool partial = (body % kRecordBytes) != 0;
        bool superseded = false;
        for (size_t i = 0; i < whole; ++i) {
            const uint8_t* r = raw.data() + kHeaderBytes + i * kRecordBytes;
            const uint32_t sector = static_cast<uint32_t>(get_le(r, 4));
            if (j->map_.count(sector)) superseded = true;
            std::array<uint8_t, kSectorBytes> a;
            std::memcpy(a.data(), r + 4, kSectorBytes);
            j->map_[sector] = a;
        }
        if (partial)
            jlog()->warn("SD write journal: {} ends in a partial record (an interrupted "
                                  "write); dropped", file);
        if (j->map_.empty()) {
            fs::remove(file, ec);
        } else if (partial || superseded) {
            std::vector<uint8_t> out = header_bytes(binding);
            for (const auto& kv : j->map_) {
                put_le(out, kv.first, 4);
                out.insert(out.end(), kv.second.begin(), kv.second.end());
            }
            if (!write_whole(file, out)) {
                why = "cannot rewrite " + file;
                return false;
            }
            j->file_len_ = out.size();
        } else {
            j->file_len_ = raw.size();
        }
    }

    std::lock_guard<std::mutex> lk(g_mu);
    g_registry[key_of(image_path)] = j;
    return true;
}

std::shared_ptr<Journal> for_image(const std::string& image_path) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_registry.empty()) return nullptr;
    auto it = g_registry.find(key_of(image_path));
    return it == g_registry.end() ? nullptr : it->second;
}

void set_persist_hook(PersistHook hook) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_hook = hook;
}

void forget_all_for_test() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_registry.clear();
    g_hook = nullptr;
}

}  // namespace sd_write_journal
