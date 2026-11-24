// Extended/self-streaming NEX tests — GH #29 / #84.
//
// Fixtures are generated at runtime. No third-party game data is shipped.
// The real-world manual oracle is Atic Atac Next v1.0b; these rows pin the
// generic contracts it exposed: lazy appended payloads, all file-handle
// forms, esxDOS file access, safe sibling reads, block-map refill, SD/MMC
// wire bytes, and the initialized-SD state inherited from NextZXOS.

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/extended_nex_host.h"
#include "core/nex_loader.h"
#include "peripheral/sd_card.h"
#include "platform/emulator_boot.h"
#include "core/log.h"

#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

int passed = 0;
int failed = 0;
int total = 0;

void check(const char* id, const char* desc, bool condition,
           const std::string& detail = {}) {
    report_row_id(id);
    ++total;
    if (condition) {
        ++passed;
    } else {
        ++failed;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

void put_u16(std::vector<uint8_t>& bytes, std::size_t off, uint16_t value) {
    bytes[off] = static_cast<uint8_t>(value);
    bytes[off + 1] = static_cast<uint8_t>(value >> 8);
}

bool write_nex(const std::filesystem::path& path, uint16_t file_handle,
               const std::vector<uint8_t>& appended, uint8_t bank_fill = 0,
               const char* version = "V1.2") {
    constexpr std::size_t bank_size = 16384;
    std::vector<uint8_t> bytes(512 + bank_size + appended.size(), 0);
    std::fill(bytes.begin() + 512, bytes.begin() + 512 + bank_size, bank_fill);
    std::memcpy(bytes.data(), "Next", 4);
    std::memcpy(bytes.data() + 4, version, 4);
    bytes[8] = 0;       // 768K requirement
    bytes[9] = 1;       // one bank
    bytes[18] = 1;      // bank 0 present
    put_u16(bytes, 12, 0xFF00);
    put_u16(bytes, 14, 0xC000);
    put_u16(bytes, 140, file_handle);
    std::copy(appended.begin(), appended.end(),
              bytes.begin() + 512 + bank_size);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

void write_zstr(Emulator& emu, uint16_t address, const std::string& value) {
    for (std::size_t i = 0; i < value.size(); ++i)
        emu.mmu().write(static_cast<uint16_t>(address + i),
                        static_cast<uint8_t>(value[i]));
    emu.mmu().write(static_cast<uint16_t>(address + value.size()), 0);
}

bool esx(Emulator& emu, uint8_t code, Z80Registers& regs) {
    return emu.cpu().on_esxdos_call &&
           emu.cpu().on_esxdos_call(code, regs);
}

bool carry(const Z80Registers& regs) { return (regs.AF & 1) != 0; }
uint8_t reg_a(const Z80Registers& regs) {
    return static_cast<uint8_t>(regs.AF >> 8);
}

void send_sd_command(SdCardDevice& sd, uint8_t cmd, uint32_t arg) {
    (void)sd.receive(static_cast<uint8_t>(0x40 | cmd));
    (void)sd.receive(static_cast<uint8_t>(arg >> 24));
    (void)sd.receive(static_cast<uint8_t>(arg >> 16));
    (void)sd.receive(static_cast<uint8_t>(arg >> 8));
    (void)sd.receive(static_cast<uint8_t>(arg));
    (void)sd.receive(0x95);
}

uint8_t first_sd_response(SdCardDevice& sd) {
    for (int i = 0; i < 16; ++i) {
        const uint8_t value = sd.send();
        if (value != 0xFF) return value;
    }
    return 0xFF;
}

// Lines of a captured "%l %v" log containing every one of `needles`.
int count_lines(const std::string& log, std::initializer_list<const char*> needles) {
    std::istringstream in(log);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        bool all = true;
        for (const char* needle : needles)
            all = all && line.find(needle) != std::string::npos;
        n += all ? 1 : 0;
    }
    return n;
}

} // namespace

int main() {
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("jnext-extended-nex-test-" + std::to_string(unique));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    const std::vector<uint8_t> payload = {
        'P', 'A', 'Y', '!', 0x10, 0x20, 0x30, 0x40
    };
    const auto closed_path = root / "closed.nex";
    const auto register_path = root / "register.nex";
    const auto memory_path = root / "memory.nex";
    check("XNEX-01", "runtime NEX fixtures are created",
          write_nex(closed_path, 0, {}) &&
          write_nex(register_path, 1, payload) &&
          write_nex(memory_path, 0xBFFE, payload));

    NexLoader loader;
    const bool parsed = loader.load(register_path.string());
    check("XNEX-02", "extended NEX parses without slurping/rejecting payload",
          parsed && loader.is_extended() && loader.keeps_file_open());
    check("XNEX-03", "payload offset and host-file size are exact",
          parsed && loader.payload_offset() == 512 + 16384 &&
          loader.file_size() == 512 + 16384 + payload.size());

    NexLoader closed_loader;
    check("XNEX-04", "file_handle=0 remains a normal closed-file load",
          closed_loader.load(closed_path.string()) &&
          !closed_loader.keeps_file_open() && !closed_loader.is_extended());

    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    cfg.load_file = register_path.string();
    Emulator emu;
    const bool init_ok = emu.init(cfg);
    const bool load_ok = init_ok && emu.load_nex(register_path.string());
    check("XNEX-05", "file_handle=1 delivers the open handle in BC",
          load_ok && emu.cpu().get_registers().BC == ExtendedNexHost::kHandle);

    Z80Registers regs{};
    regs.AF = static_cast<uint16_t>(ExtendedNexHost::kHandle << 8);
    regs.DE = static_cast<uint16_t>(512 + 16384);
    regs.IX = 0; // IXL=ESX_SEEK_SET
    const bool seek_payload = esx(emu, 0x9F, regs) && !carry(regs);
    regs.AF = static_cast<uint16_t>(ExtendedNexHost::kHandle << 8);
    regs.IX = 0x9000;
    regs.BC = 4;
    const bool read_payload = esx(emu, 0x9D, regs) && !carry(regs);
    check("XNEX-06", "F_SEEK uses IXL and F_READ reaches appended payload",
          seek_payload && read_payload && regs.BC == 4 &&
          emu.mmu().read(0x9000) == 'P' && emu.mmu().read(0x9003) == '!');

    regs = {};
    regs.AF = static_cast<uint16_t>(ExtendedNexHost::kHandle << 8);
    const bool got_pos = esx(emu, 0xA0, regs) && !carry(regs);
    check("XNEX-07", "F_FGETPOS returns the 32-bit post-read position in BCDE",
          got_pos && regs.BC == 0 && regs.DE == 512 + 16384 + 4);

    regs = {};
    regs.AF = static_cast<uint16_t>(ExtendedNexHost::kHandle << 8);
    regs.IX = 0x9100;
    regs.HL = 0x9200; // poison the register used by the pre-review bug
    for (int i = 0; i < 11; ++i) emu.mmu().write(0x9200 + i, 0xA5);
    const bool stat_ok = esx(emu, 0xA1, regs) && !carry(regs);
    const uint32_t stat_size =
        static_cast<uint32_t>(emu.mmu().read(0x9107)) |
        (static_cast<uint32_t>(emu.mmu().read(0x9108)) << 8) |
        (static_cast<uint32_t>(emu.mmu().read(0x9109)) << 16) |
        (static_cast<uint32_t>(emu.mmu().read(0x910A)) << 24);
    check("XNEX-08", "F_FSTAT reports the complete extended host-file size",
          stat_ok && stat_size == 512 + 16384 + payload.size() &&
          emu.mmu().read(0x9200) == 0xA5 &&
          emu.mmu().read(0x920A) == 0xA5);

    const auto cfg_path = root / "register.cfg";
    {
        std::ofstream out(cfg_path, std::ios::binary);
        out << "KEYS=Q,A,O,P";
    }
    write_zstr(emu, 0x9300, "register.cfg");
    regs = {};
    regs.IX = 0x9300;
    regs.BC = 0x0100;
    const bool sibling_open = esx(emu, 0x9A, regs) && !carry(regs) &&
                              reg_a(regs) == ExtendedNexHost::kSiblingHandle;
    regs.AF = static_cast<uint16_t>(ExtendedNexHost::kSiblingHandle << 8);
    regs.IX = 0x9400;
    regs.BC = 4;
    const bool sibling_read = esx(emu, 0x9D, regs) && !carry(regs);
    check("XNEX-09", "read-only sibling config opens on a distinct handle",
          sibling_open && sibling_read &&
          emu.mmu().read(0x9400) == 'K' && emu.mmu().read(0x9403) == 'S');

    write_zstr(emu, 0x9300, "../escape.cfg");
    regs = {};
    regs.IX = 0x9300;
    regs.BC = 0x0100;
    check("XNEX-10", "parent-directory escape is refused",
          esx(emu, 0x9A, regs) && carry(regs));

    write_zstr(emu, 0x9300, cfg_path.string());
    regs = {};
    regs.IX = 0x9300;
    regs.BC = 0x0100;
    check("XNEX-11", "absolute sibling path is refused",
          esx(emu, 0x9A, regs) && carry(regs));

    const auto link_path = root / "linked.cfg";
    std::filesystem::create_symlink(cfg_path, link_path, ec);
    const bool symlink_supported = !ec;
    write_zstr(emu, 0x9300, "linked.cfg");
    regs = {};
    regs.IX = 0x9300;
    regs.BC = 0x0100;
    const bool symlink_refused =
        symlink_supported && esx(emu, 0x9A, regs) && carry(regs);
    check("XNEX-12", "symlink sibling is refused",
          !symlink_supported || symlink_refused,
          symlink_supported ? std::string{} :
                              "host does not permit symlink creation");

    write_zstr(emu, 0x9300, "register.cfg");
    regs = {};
    regs.IX = 0x9300;
    regs.BC = 0x0200;
    check("XNEX-13", "companion writes are refused",
          esx(emu, 0x9A, regs) && carry(regs) && reg_a(regs) == 0x08);

    // XNEX-28 (PR #100 review finding 3, hardening): a BARE ".." has no
    // parent_path() on POSIX and therefore slips past the plain-name gate,
    // resolving to the grandparent directory. Pin the contract that it is
    // refused — the explicit dot-name guard makes the rejection deliberate
    // rather than an incidental side effect of the is_regular_file() check.
    write_zstr(emu, 0x9300, "..");
    regs = {};
    regs.IX = 0x9300;
    regs.BC = 0x0100;
    check("XNEX-28", "bare dot-dot companion name is refused",
          esx(emu, 0x9A, regs) && carry(regs));

    regs = {};
    regs.AF = static_cast<uint16_t>(ExtendedNexHost::kHandle << 8);
    regs.IX = 0;
    const bool rewind_ok = esx(emu, 0x9F, regs) && !carry(regs);
    regs.AF = static_cast<uint16_t>(ExtendedNexHost::kHandle << 8);
    regs.IX = 0x9500;
    regs.DE = 2;
    const bool map_ok = esx(emu, 0x85, regs) && !carry(regs);
    const uint16_t mapped_blocks =
        static_cast<uint16_t>(emu.mmu().read(0x9504) |
                              (emu.mmu().read(0x9505) << 8));
    check("XNEX-14", "DISK_FILEMAP returns a contiguous block-addressed extent",
          rewind_ok && map_ok && reg_a(regs) == ExtendedNexHost::kCardFlags &&
          regs.DE == 1 && regs.HL == 0x9506 &&
          mapped_blocks == (loader.file_size() + 511) / 512);

    regs = {};
    regs.AF = 0x8200; // card flags + NextZXOS 2.01 no-wait bit
    regs.IX = static_cast<uint16_t>(
        ExtendedNexHost::kSyntheticFirstBlock >> 16);
    regs.DE = static_cast<uint16_t>(
        ExtendedNexHost::kSyntheticFirstBlock);
    const bool stream_started = esx(emu, 0x86, regs) && !carry(regs) &&
                                regs.BC == 0x00EB;
    const uint8_t ready = emu.port().in(0x00EB);
    const uint8_t n = emu.port().in(0x00EB);
    const uint8_t e = emu.port().in(0x00EB);
    const uint8_t x = emu.port().in(0x00EB);
    const uint8_t t = emu.port().in(0x00EB);
    check("XNEX-15", "no-wait stream emits FE token then host-file bytes",
          stream_started && ready == 0xFE &&
          n == 'N' && e == 'e' && x == 'x' && t == 't');
    regs.AF = 0x0200;
    check("XNEX-16", "DISK_STRMEND closes the host streaming window",
          esx(emu, 0x87, regs) && !carry(regs));

    Emulator memory_emu;
    EmulatorConfig memory_cfg = cfg;
    memory_cfg.load_file = memory_path.string();
    const bool memory_ok = memory_emu.init(memory_cfg) &&
                           memory_emu.load_nex(memory_path.string());
    check("XNEX-17", "file_handle>=0x4000 writes the handle to guest memory",
          memory_ok &&
          memory_emu.mmu().read(0xBFFE) == ExtendedNexHost::kHandle);

    const auto stream_path = root / "partial.bin";
    {
        std::ofstream out(stream_path, std::ios::binary);
        for (int i = 0; i < 700; ++i)
            out.put(static_cast<char>(i & 0xFF));
    }
    ExtendedNexHost host;
    const bool host_open = host.open(stream_path.string());
    const bool host_start = host_open &&
                            host.stream_start(
                                ExtendedNexHost::kSyntheticFirstBlock, 0x80);
    const auto token = host.read_stream_byte();
    bool first_block = token && *token == 0xFE;
    bool interleave_ok = true;
    for (int i = 0; i < 512 && first_block; ++i) {
        // F_READ and raw synthetic-sector reads seek the primary handle.
        // They must not move the independent port-$EB stream.
        if (i == 128) {
            uint8_t scratch[512]{};
            std::size_t actual = 0;
            interleave_ok =
                host.seek(0, 600) &&
                host.read(scratch, 8, actual) && actual == 8 &&
                host.read_block(
                    ExtendedNexHost::kSyntheticFirstBlock, scratch);
        }
        const auto byte = host.read_stream_byte();
        first_block =
            interleave_ok && byte && *byte == static_cast<uint8_t>(i);
    }
    const auto crc_hi = host.read_stream_byte();
    const auto crc_lo = host.read_stream_byte();
    const auto next_token = host.read_stream_byte();
    check("XNEX-18", "stream crosses a full 512-byte block with CRC/token framing",
          host_start && interleave_ok && first_block && crc_hi && *crc_hi == 0 &&
          crc_lo && *crc_lo == 0 && next_token && *next_token == 0xFE);

    bool partial_and_pad = true;
    for (int i = 512; i < 1024 && partial_and_pad; ++i) {
        const auto byte = host.read_stream_byte();
        const uint8_t want = i < 700 ? static_cast<uint8_t>(i) : 0;
        partial_and_pad = byte && *byte == want;
    }
    check("XNEX-19", "partial final sector is zero-padded deterministically",
          partial_and_pad);

    const auto sparse_path = root / "sparse.bin";
    {
        std::ofstream out(sparse_path, std::ios::binary);
        out.put(0);
    }
    std::filesystem::resize_file(
        sparse_path,
        (static_cast<uint64_t>(65536) * ExtendedNexHost::kBlockSize) + 1,
        ec);
    ExtendedNexHost sparse;
    std::vector<ExtendedNexHost::FileMapEntry> first_map;
    std::vector<ExtendedNexHost::FileMapEntry> refill_map;
    const bool sparse_ok = !ec && sparse.open(sparse_path.string()) &&
                           sparse.file_map(1, first_map) &&
                           sparse.file_map(1, refill_map);
    check("XNEX-20", "file-map refill splits extents at the uint16 block limit",
          sparse_ok && first_map.size() == 1 && refill_map.size() == 1 &&
          first_map[0].address == ExtendedNexHost::kSyntheticFirstBlock &&
          first_map[0].blocks == 65535 &&
          refill_map[0].address ==
              ExtendedNexHost::kSyntheticFirstBlock + 65535 &&
          refill_map[0].blocks == 2);

    const auto sd_path = root / "card.img";
    {
        std::ofstream out(sd_path, std::ios::binary);
        std::vector<uint8_t> sector(512, 0x5A);
        out.write(reinterpret_cast<const char*>(sector.data()), sector.size());
    }
    SdCardDevice sd;
    const bool mounted = sd.mount(sd_path.string());
    sd.prepare_for_direct_nex();
    send_sd_command(sd, 17, 0);
    const uint8_t r1 = first_sd_response(sd);
    check("XNEX-21", "direct NEX inherits an initialized mounted SDHC card",
          mounted && r1 == 0x00,
          "CMD17 R1=" + std::to_string(r1));

    const bool emu_card_mounted = emu.sd_card().mount(sd_path.string());
    const bool overlay_reloaded =
        emu_card_mounted && emu.load_nex(register_path.string());
    emu.port().out(0x00E7, 0xFE);
    (void)emu.port().in(0x00EB);  // consume the SPI master's prior latch
    const uint32_t synthetic = ExtendedNexHost::kSyntheticFirstBlock;
    const uint8_t raw_cmd[] = {
        0x52,
        static_cast<uint8_t>(synthetic >> 24),
        static_cast<uint8_t>(synthetic >> 16),
        static_cast<uint8_t>(synthetic >> 8),
        static_cast<uint8_t>(synthetic),
        0x80
    };
    for (uint8_t byte : raw_cmd) emu.port().out(0x00EB, byte);
    uint8_t raw_ready = 0xFF;
    for (int i = 0; i < 16 && raw_ready == 0xFF; ++i)
        raw_ready = emu.port().in(0x00EB);
    const uint8_t raw_r1 = emu.port().in(0x00EB);
    const uint8_t raw_token = emu.port().in(0x00EB);
    const uint8_t raw_n = emu.port().in(0x00EB);
    const uint8_t raw_e = emu.port().in(0x00EB);
    const uint8_t raw_x = emu.port().in(0x00EB);
    const uint8_t raw_t = emu.port().in(0x00EB);
    check("XNEX-22", "raw SD CMD18 reads the synthetic NEX file-map range",
          overlay_reloaded && raw_ready == 0xFE &&
          raw_r1 == 0x00 && raw_token == 0xFE &&
          raw_n == 'N' && raw_e == 'e' && raw_x == 'x' && raw_t == 't');

    // Atic's direct CMD18 reader consumes the compatibility sequence used by
    // ZEsarUX: each 512-byte chunk is followed immediately by FE,00,FE and
    // then the next chunk. This deliberately differs from the real-card path,
    // which emits CRC16 and an inter-block filler.
    for (int i = 4; i < 512; ++i) (void)emu.port().in(0x00EB);
    const uint8_t block2_ready = emu.port().in(0x00EB);
    const uint8_t block2_r1 = emu.port().in(0x00EB);
    const uint8_t block2_token = emu.port().in(0x00EB);
    const uint8_t block2_first = emu.port().in(0x00EB);
    for (int i = 1; i < 512; ++i) (void)emu.port().in(0x00EB);
    const uint8_t block3_ready = emu.port().in(0x00EB);
    const uint8_t block3_r1 = emu.port().in(0x00EB);
    const uint8_t block3_token = emu.port().in(0x00EB);
    check("XNEX-23", "raw CMD18 preserves FE,00,FE framing across blocks",
          block2_ready == 0xFE && block2_r1 == 0x00 &&
          block2_token == 0xFE && block2_first == 0x00 &&
          block3_ready == 0xFE && block3_r1 == 0x00 &&
          block3_token == 0xFE);

    // File -> Open loads happen after init(), so no command-line NEX exists
    // when the CPU hook is configured. The bridge must remain dormant for the
    // plain emulator, then attach when load_nex() supplies a host-backed file.
    Emulator gui_emu;
    EmulatorConfig gui_cfg;
    gui_cfg.type = MachineType::ZXN_ISSUE2;
    gui_cfg.rewind_buffer_frames = 0;
    const bool gui_init = gui_emu.init(gui_cfg);
    const bool dormant_before_load =
        gui_init && !gui_emu.cpu().on_esxdos_call;
    const bool gui_load =
        gui_init && gui_emu.load_nex(register_path.string());
    regs = {};
    const bool gui_version =
        gui_load && esx(gui_emu, 0x88, regs) && !carry(regs) &&
        regs.DE == 0x0202;
    check("XNEX-24", "GUI-style post-init NEX load activates the host bridge",
          dormant_before_load && gui_version);

    regs = {};
    regs.AF = 0x8200;
    regs.IX = static_cast<uint16_t>(
        ExtendedNexHost::kSyntheticFirstBlock >> 16);
    regs.DE = static_cast<uint16_t>(
        ExtendedNexHost::kSyntheticFirstBlock);
    const bool gui_stream =
        esx(gui_emu, 0x86, regs) && !carry(regs) &&
        gui_emu.port().in(0x00EB) == 0xFE;
    gui_emu.soft_reset();
    regs = {};
    const bool soft_bridge_gone = !esx(gui_emu, 0x88, regs);
    const uint8_t soft_port = gui_emu.port().in(0x00EB);
    check("XNEX-25", "soft reset disarms host calls and an active EB stream",
          gui_stream && soft_bridge_gone && soft_port != 0xFE &&
          !gui_emu.sd_card().has_read_overlay());

    const bool reload_after_soft =
        gui_emu.load_nex(register_path.string());
    regs = {};
    const bool rearmed_after_soft =
        reload_after_soft && esx(gui_emu, 0x88, regs) && !carry(regs);
    // GH #239 — the production hard reset (F1, Machine > Power Reset, a
    // guest's NR 0x02 bit 1): the frontend cold boot, with no file to load.
    // That boot attaches the RST $08 hook only for --esxdos-stub or with the
    // esxdos logger at trace; with no hook, "not answered" would hold by
    // absence. So the reset runs with esxdos at trace (as
    // `--log-level esxdos=trace` then F1 does) and the row REQUIRES the hook,
    // whose answer is then the evidence that the bridge was disarmed.
    const auto esx_level = Log::esxdos()->level();
    Log::esxdos()->set_level(spdlog::level::trace);
    emulator_frontend_cold_boot(gui_emu, gui_emu.config(), std::string(),
                                ColdBootHooks{});
    regs = {};
    const bool hard_hook = static_cast<bool>(gui_emu.cpu().on_esxdos_call);
    const bool hard_bridge_gone = hard_hook && !esx(gui_emu, 0x88, regs);
    Log::esxdos()->set_level(esx_level);
    check("XNEX-26", "hard reset (frontend cold boot) disarms a reloaded host bridge: "
          "with the hook present (esxdos tracing on) M_DOSVERSION is no longer "
          "answered and the SD read overlay is gone",
          rearmed_after_soft && hard_hook && hard_bridge_gone &&
          !gui_emu.sd_card().has_read_overlay());

    // A command-line load reattaches the dormant hook on a SOFT reset, because
    // init() re-runs with config_.load_file still a NEX path. (A hard reset
    // cold-boots with the load file cleared, so it attaches no hook at all.)
    // The hook must nevertheless decline the call once the reset has cleared
    // the host path, allowing the real ROM's RST $08 handler to run.
    Emulator cli_emu;
    EmulatorConfig cli_cfg = cfg;
    const bool cli_loaded =
        cli_emu.init(cli_cfg) &&
        cli_emu.load_nex(register_path.string());
    cli_emu.soft_reset();
    regs = {};
    const bool cli_hook_present =
        static_cast<bool>(cli_emu.cpu().on_esxdos_call);
    const bool cli_bridge_gone = !esx(cli_emu, 0x88, regs);
    check("XNEX-27", "CLI soft reset keeps a dormant hook without servicing host calls",
          cli_loaded && cli_hook_present && cli_bridge_gone &&
          !cli_emu.sd_card().has_read_overlay());

    // GH #250: a NEX whose header says file_handle=0 but whose file carries
    // more than 16K after its declared banks (Spectron2084: 293120 bytes).
    // The real loaders read the declared banks, close the file and run the
    // program — no size check, nothing reads the rest (nexload2.asm:390-395,
    // nexload.asm:547-551). jnext loads it the same way, with a warning.
    std::string trailing;
    while (trailing.size() < 20000) trailing += "GH250-TRAILING-PAYLOAD ";
    const std::vector<uint8_t> big_payload(trailing.begin(), trailing.end());
    const std::vector<uint8_t> padding(16384, 0xEE);
    const auto closed_ext_path = root / "closed-extended.nex";
    const auto register_ext_path = root / "register-extended.nex";
    const auto closed_pad_path = root / "closed-padded.nex";
    const bool gh250_fixtures =
        write_nex(closed_ext_path, 0, big_payload, 0x5A) &&
        write_nex(register_ext_path, 1, big_payload, 0x5A) &&
        write_nex(closed_pad_path, 0, padding, 0x5A);

    std::ostringstream log_out;
    auto log_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(log_out);
    log_sink->set_pattern("%l %v");
    Log::emulator()->sinks().push_back(log_sink);

    NexLoader closed_ext;
    const bool closed_ext_ok = gh250_fixtures && closed_ext.load(closed_ext_path.string());
    const std::string closed_ext_log = log_out.str();
    check("XNEX-29", "file_handle=0 with a >16K trailing payload loads (GH #250)",
          closed_ext_ok && !closed_ext.keeps_file_open() &&
          closed_ext.payload_offset() == 512 + 16384 &&
          closed_ext.file_size() == 512 + 16384 + big_payload.size());
    check("XNEX-30", "that load logs one warning that the trailing bytes are ignored",
          count_lines(closed_ext_log, {"warning ", "extended NEX file", "ignoring the trailing"}) == 1 &&
          count_lines(closed_ext_log, {"error "}) == 0 &&
          count_lines(closed_ext_log, {"trailing padding"}) == 0,
          closed_ext_log);

    // Heap-allocated: main() already holds several Emulators on the stack.
    auto closed_emu_ptr = std::make_unique<Emulator>();
    Emulator& closed_emu = *closed_emu_ptr;
    EmulatorConfig closed_cfg = cfg;
    closed_cfg.load_file = closed_ext_path.string();
    const bool closed_loaded = closed_emu.init(closed_cfg) &&
                               closed_emu.load_nex(closed_ext_path.string());
    // The whole bank is the file's 0x5A except the word just below the entry
    // SP ($FF00, write_nex()): the NextZXOS RST $20 both loaders leave through
    // pushes the entry PC ($C000) there (NEXENT-19).
    bool bank_ok = closed_loaded;
    for (uint32_t a = 0xC000; a <= 0xFFFF && bank_ok; ++a) {
        const uint8_t want = a == 0xFEFE ? 0x00 : a == 0xFEFF ? 0xC0 : 0x5A;
        bank_ok = closed_emu.mmu().read(static_cast<uint16_t>(a)) == want;
    }
    const uint8_t* sram = closed_emu.ram().page_ptr(0);
    const std::string marker = "GH250-TRAILING";
    const bool payload_in_ram =
        sram && std::search(sram, sram + closed_emu.ram().size(),
                            marker.begin(), marker.end()) != sram + closed_emu.ram().size();
    // No host bridge either: the direct-load esxDOS stand-in (GH #250)
    // answers, but reports 1.94 (2.02 only with the bridge) and cannot open
    // the NEX's own file, which only the bridge serves.
    regs = {};
    const bool dos_answered = esx(closed_emu, 0x88, regs) && !carry(regs);
    const uint16_t dos_version = regs.DE;
    write_zstr(closed_emu, 0x9300, closed_ext_path.filename().string());
    regs = {};
    regs.IX = 0x9300;
    regs.BC = 0x0100;  // read-only
    const bool own_open_refused = esx(closed_emu, 0x9A, regs) && carry(regs);
    check("XNEX-31", "only the declared bank is loaded; the payload reaches no guest path",
          closed_loaded && bank_ok && !payload_in_ram &&
          !closed_emu.sd_card().has_read_overlay() &&
          dos_answered && dos_version == 0x0194 && own_open_refused);

    log_out.str("");
    NexLoader register_ext;
    const bool register_ext_ok =
        gh250_fixtures && register_ext.load(register_ext_path.string());
    const std::string register_ext_log = log_out.str();
    check("XNEX-32", "file_handle=1 with the same payload stays host-backed, no warning",
          register_ext_ok && register_ext.keeps_file_open() && register_ext.is_extended() &&
          count_lines(register_ext_log, {"warning "}) == 0 &&
          count_lines(register_ext_log, {"info ", "keeping them host-backed"}) == 1,
          register_ext_log);

    log_out.str("");
    NexLoader closed_pad;
    const bool closed_pad_ok = gh250_fixtures && closed_pad.load(closed_pad_path.string());
    const std::string closed_pad_log = log_out.str();
    check("XNEX-33", "file_handle=0 with <=16K trailing padding loads without a warning",
          closed_pad_ok && !closed_pad.keeps_file_open() &&
          count_lines(closed_pad_log, {"warning "}) == 0 &&
          count_lines(closed_pad_log, {"info ", "trailing padding"}) == 1,
          closed_pad_log);

    // Both loaders keep the file open for ANY non-zero file_handle; the value
    // only picks where the handle goes: 1..0x3FFF in BC (B=0, C=handle),
    // 0x4000 and above written to that address (nexload2.asm:390-407,
    // nexload.asm:547-570, :606-609). XNEX-05 pins 1, XNEX-17 0xBFFE.
    const auto bc2_path = root / "handle-bc-2.nex";
    const auto bc3fff_path = root / "handle-bc-3fff.nex";
    const auto mem4000_path = root / "handle-mem-4000.nex";
    const bool handle_fixtures =
        write_nex(bc2_path, 0x0002, big_payload) &&
        write_nex(bc3fff_path, 0x3FFF, payload) &&
        write_nex(mem4000_path, 0x4000, payload);

    log_out.str("");
    auto bc2_emu_ptr = std::make_unique<Emulator>();
    Emulator& bc2_emu = *bc2_emu_ptr;
    EmulatorConfig bc2_cfg = cfg;
    bc2_cfg.load_file = bc2_path.string();
    NexLoader bc2_loader;
    const bool bc2_parsed = handle_fixtures && bc2_loader.load(bc2_path.string());
    const bool bc2_loaded = bc2_parsed && bc2_emu.init(bc2_cfg) &&
                            bc2_emu.load_nex(bc2_path.string());
    const std::string bc2_log = log_out.str();
    regs = {};
    regs.AF = static_cast<uint16_t>(ExtendedNexHost::kHandle << 8);
    regs.DE = static_cast<uint16_t>(512 + 16384);
    regs.IX = 0; // IXL=ESX_SEEK_SET
    const bool bc2_seek = bc2_loaded && esx(bc2_emu, 0x9F, regs) && !carry(regs);
    regs.AF = static_cast<uint16_t>(ExtendedNexHost::kHandle << 8);
    regs.IX = 0x9000;
    regs.BC = 5;
    const bool bc2_read = bc2_seek && esx(bc2_emu, 0x9D, regs) && !carry(regs) &&
                          regs.BC == 5;
    std::string bc2_bytes;
    for (uint16_t i = 0; i < 5; ++i)
        bc2_bytes.push_back(static_cast<char>(bc2_emu.mmu().read(0x9000 + i)));
    check("XNEX-34", "file_handle=2 keeps the file open, handle in BC, payload streams",
          bc2_loaded && bc2_loader.keeps_file_open() &&
          bc2_loader.delivers_handle_in_bc() &&
          bc2_emu.cpu().get_registers().BC == ExtendedNexHost::kHandle &&
          bc2_read && bc2_bytes == "GH250" &&
          count_lines(bc2_log, {"warning "}) == 0,
          bc2_log);

    auto bc3fff_emu_ptr = std::make_unique<Emulator>();
    Emulator& bc3fff_emu = *bc3fff_emu_ptr;
    EmulatorConfig bc3fff_cfg = cfg;
    bc3fff_cfg.load_file = bc3fff_path.string();
    NexLoader bc3fff_loader;
    const bool bc3fff_loaded =
        handle_fixtures && bc3fff_loader.load(bc3fff_path.string()) &&
        bc3fff_emu.init(bc3fff_cfg) && bc3fff_emu.load_nex(bc3fff_path.string());
    check("XNEX-35", "file_handle=0x3FFF (last BC value) delivers the handle in BC",
          bc3fff_loaded && bc3fff_loader.keeps_file_open() &&
          bc3fff_loader.delivers_handle_in_bc() &&
          bc3fff_emu.cpu().get_registers().BC == ExtendedNexHost::kHandle &&
          bc3fff_emu.sd_card().has_read_overlay());

    auto mem4000_emu_ptr = std::make_unique<Emulator>();
    Emulator& mem4000_emu = *mem4000_emu_ptr;
    EmulatorConfig mem4000_cfg = cfg;
    mem4000_cfg.load_file = mem4000_path.string();
    NexLoader mem4000_loader;
    const bool mem4000_loaded =
        handle_fixtures && mem4000_loader.load(mem4000_path.string()) &&
        mem4000_emu.init(mem4000_cfg) && mem4000_emu.load_nex(mem4000_path.string());
    check("XNEX-36", "file_handle=0x4000 (first address) writes the handle there, not BC",
          mem4000_loaded && mem4000_loader.keeps_file_open() &&
          !mem4000_loader.delivers_handle_in_bc() &&
          mem4000_emu.mmu().read(0x4000) == ExtendedNexHost::kHandle &&
          mem4000_emu.cpu().get_registers().BC != ExtendedNexHost::kHandle);

    // BC at entry when no handle travels in it. Each loader ends with an
    // `ld bc,nn` whose low byte only the handle-in-BC case patches; the
    // default immediates differ, and jnext follows the loader that really
    // runs each version: V1.3 -> nexload2.asm:407 `ld bc,255`; V1.0-V1.2 ->
    // the distro nexload.asm:582-585 `db 01 / db 0 / db 0` (`ld bc,$0000`).
    //
    // XNEX-37/38 are the regression rows for the load_nex() BC write: without
    // it, V1.3 enters with BC=$0000. XNEX-39 guards the handle-in-BC case
    // against that write. XNEX-40/41 are NOT regression rows for it — BC is
    // already $0000 after load_nex()'s init(config_) re-initialisation, so
    // they pass with the write removed. They pin V1.0-V1.2 to the distro
    // loader's $0000 against
    // a "use nexload2's 255 for every version" change, which they do catch.
    struct BcLoad { bool ok = false; uint16_t bc = 0; uint8_t at_4000 = 0; };
    auto load_bc = [&](const char* name, const char* version, uint16_t file_handle) {
        BcLoad r;
        const auto path = root / name;
        auto emu_ptr = std::make_unique<Emulator>();
        EmulatorConfig bc_cfg = cfg;
        bc_cfg.load_file = path.string();
        bc_cfg.allow_experimental_nex_v13 = true;   // V1.3 is opt-in (GH #228)
        r.ok = write_nex(path, file_handle, payload, 0, version) &&
               emu_ptr->init(bc_cfg) && emu_ptr->load_nex(path.string());
        r.bc = emu_ptr->cpu().get_registers().BC;
        r.at_4000 = emu_ptr->mmu().read(0x4000);
        return r;
    };
    auto bc_detail = [](const BcLoad& r) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "ok=%d BC=0x%04X", r.ok ? 1 : 0, r.bc);
        return std::string(buf);
    };

    const BcLoad v13_closed = load_bc("bc-v13-closed.nex", "V1.3", 0x0000);
    check("XNEX-37", "V1.3, file_handle=0: BC=$00FF at entry (nexload2.asm:407 ld bc,255)",
          v13_closed.ok && v13_closed.bc == 0x00FF, bc_detail(v13_closed));

    const BcLoad v13_mem = load_bc("bc-v13-mem.nex", "V1.3", 0x4000);
    check("XNEX-38", "V1.3, file_handle=0x4000: handle in memory, BC=$00FF (nexload2.asm:400-407)",
          v13_mem.ok && v13_mem.at_4000 == ExtendedNexHost::kHandle &&
          v13_mem.bc == 0x00FF, bc_detail(v13_mem));

    const BcLoad v13_bc = load_bc("bc-v13-bc.nex", "V1.3", 0x0001);
    check("XNEX-39", "V1.3, file_handle=1: the handle, not 255, is in BC (nexload2.asm:401-404)",
          v13_bc.ok && v13_bc.bc == ExtendedNexHost::kHandle, bc_detail(v13_bc));

    const BcLoad v12_closed = load_bc("bc-v12-closed.nex", "V1.2", 0x0000);
    check("XNEX-40", "V1.2, file_handle=0: BC=$0000, the distro loader's value, not nexload2's "
          "255 (nexload.asm:582-585 ld bc,$0000; version pin, not a regression row)",
          v12_closed.ok && v12_closed.bc == 0x0000, bc_detail(v12_closed));

    const BcLoad v12_mem = load_bc("bc-v12-mem.nex", "V1.2", 0x4000);
    check("XNEX-41", "V1.2, file_handle=0x4000: handle in memory, BC=$0000, not nexload2's 255 "
          "(nexload.asm:560-585; version pin, not a regression row)",
          v12_mem.ok && v12_mem.at_4000 == ExtendedNexHost::kHandle &&
          v12_mem.bc == 0x0000, bc_detail(v12_mem));

    // GH #267 — the kept-open handle starts where the loader's bank loading
    // left it, just after the last bank: nexload2.asm:390-407
    // (.passHandleToApp) and nexload.asm:547-570 pass it on without a seek,
    // so the first bare F_READ returns the appended payload. Measured under
    // NextZXOS: a probe NEX doing a bare 4-byte F_READ on its handle gets the
    // payload's first bytes, and F_FGETPOS then says payload + 4. The rows
    // before these all seek first, so none looked at what a bare read gets.
    struct BareRead { bool ok = false; std::string bytes; uint32_t pos = 0; };
    auto bare_read = [&](const std::filesystem::path& path, uint16_t file_handle) {
        BareRead r;
        auto emu_ptr = std::make_unique<Emulator>();
        EmulatorConfig bare_cfg = cfg;
        bare_cfg.load_file = path.string();
        const bool loaded = write_nex(path, file_handle, payload) &&
                            emu_ptr->init(bare_cfg) && emu_ptr->load_nex(path.string());
        Z80Registers rr{};
        rr.AF = static_cast<uint16_t>(ExtendedNexHost::kHandle << 8);
        rr.IX = 0x9000;
        rr.BC = 4;
        const bool read_ok = loaded && esx(*emu_ptr, 0x9D, rr) && !carry(rr) && rr.BC == 4;
        for (uint16_t i = 0; i < 4; ++i)
            r.bytes.push_back(static_cast<char>(emu_ptr->mmu().read(0x9000 + i)));
        rr = {};
        rr.AF = static_cast<uint16_t>(ExtendedNexHost::kHandle << 8);
        const bool pos_ok = read_ok && esx(*emu_ptr, 0xA0, rr) && !carry(rr);
        r.pos = (static_cast<uint32_t>(rr.BC) << 16) | rr.DE;
        r.ok = pos_ok;
        return r;
    };
    const BareRead bare_bc = bare_read(root / "bare-bc.nex", 0x0001);
    check("XNEX-42",
          "file_handle=1: a bare F_READ (no seek) returns the first payload bytes, and "
          "F_FGETPOS then reports payload offset + 4 (GH #267)",
          bare_bc.ok && bare_bc.bytes == "PAY!" && bare_bc.pos == 512 + 16384 + 4,
          "bytes=" + bare_bc.bytes + " pos=" + std::to_string(bare_bc.pos));
    const BareRead bare_mem = bare_read(root / "bare-mem.nex", 0xBFFE);
    check("XNEX-43",
          "file_handle=$BFFE (handle written to memory): a bare F_READ also starts at the "
          "payload (GH #267)",
          bare_mem.ok && bare_mem.bytes == "PAY!" && bare_mem.pos == 512 + 16384 + 4,
          "bytes=" + bare_mem.bytes + " pos=" + std::to_string(bare_mem.pos));
    {
        // Control: the program's OWN F_OPEN of its file is a fresh open, at 0.
        auto emu_ptr = std::make_unique<Emulator>();
        const auto path = root / "reopen.nex";
        EmulatorConfig reopen_cfg = cfg;
        reopen_cfg.load_file = path.string();
        const bool loaded = write_nex(path, 0x0001, payload) &&
                            emu_ptr->init(reopen_cfg) && emu_ptr->load_nex(path.string());
        Z80Registers rr{};
        if (loaded) write_zstr(*emu_ptr, 0x9100, "reopen.nex");
        rr.AF = static_cast<uint16_t>('*' << 8);
        rr.BC = 0x0100;   // B = FA_READ
        rr.IX = 0x9100;
        const bool open_ok = loaded && esx(*emu_ptr, 0x9A, rr) && !carry(rr);
        const uint8_t h = reg_a(rr);
        rr = {};
        rr.AF = static_cast<uint16_t>(h << 8);
        rr.IX = 0x9000;
        rr.BC = 4;
        const bool read_ok = open_ok && esx(*emu_ptr, 0x9D, rr) && !carry(rr);
        std::string bytes;
        for (uint16_t i = 0; i < 4; ++i)
            bytes.push_back(static_cast<char>(emu_ptr->mmu().read(0x9000 + i)));
        check("XNEX-44",
              "control: the program's own F_OPEN of its file starts at 0 — a bare F_READ "
              "then returns the header's \"Next\"",
              read_ok && h == ExtendedNexHost::kHandle && bytes == "Next",
              "handle=" + std::to_string(h) + " bytes=" + bytes);
    }

    // ── GH #556 — block streaming of the host directory's files ──────────────
    // The Way of the Exploding Fist streams its data files with F_OPEN,
    // DISK_FILEMAP, DISK_STRMSTART and port $EB reads; under --load the files
    // come from the host directory, which used to refuse $85-$87 (Fc=1, A=2)
    // and sent the game to DI : HALT. Oracle: the NextZXOS API (DISK_FILEMAP
    // p.38, DISK_STRMSTART p.39, DISK_STRMEND p.40) and tbblue
    // src/asm/streaming/stream.asm. No VHDL is involved (the esxDOS stand-in
    // is an emulator convenience). Fixtures are generated; no game data.
    const auto hroot = root / "hostdir";
    std::filesystem::create_directories(hroot / "data", ec);
    constexpr std::size_t s_size = 1300 * 512 + 100;
    auto sbyte = [](std::size_t k) { return static_cast<uint8_t>((k * 7 + 3) & 255); };
    auto tbyte = [](std::size_t k) { return static_cast<uint8_t>((k * 5 + 1) & 255); };
    {
        std::ofstream s(hroot / "data" / "s.bin", std::ios::binary);
        for (std::size_t k = 0; k < s_size; ++k) s.put(static_cast<char>(sbyte(k)));
        std::ofstream t(hroot / "data" / "t.bin", std::ios::binary);
        for (std::size_t k = 0; k < 3 * 512; ++k) t.put(static_cast<char>(tbyte(k)));
        std::ofstream b(hroot / "data" / "big.bin", std::ios::binary);
        b.put(0);
    }
    std::filesystem::resize_file(hroot / "data" / "big.bin",
                                 static_cast<uint64_t>(40) * 1024 * 1024, ec);
    const bool h_fixtures = !ec &&
        write_nex(hroot / "host0.nex", 0, {}) &&
        write_nex(hroot / "host1.nex", 1, payload);

    auto host_emu = [&](const char* nex) {
        auto e = std::make_unique<Emulator>();
        EmulatorConfig hc = cfg;
        hc.esxdos_stub = true;
        hc.load_file = (hroot / nex).string();
        const bool ok = e->init(hc) && e->load_nex(hc.load_file);
        return ok ? std::move(e) : std::unique_ptr<Emulator>();
    };
    auto h_open = [&](Emulator& e, const char* name) {
        write_zstr(e, 0x9100, name);
        Z80Registers rr{};
        rr.AF = static_cast<uint16_t>('*' << 8);
        rr.BC = 0x0100;   // B = FA_READ
        rr.IX = 0x9100;
        return esx(e, 0x9A, rr) && !carry(rr) ? static_cast<int>(reg_a(rr)) : -1;
    };
    auto h_close = [&](Emulator& e, int h) {
        Z80Registers rr{};
        rr.AF = static_cast<uint16_t>(h << 8);
        return esx(e, 0x9B, rr) && !carry(rr);
    };
    auto h_map = [&](Emulator& e, int h, uint16_t ix, uint16_t de, Z80Registers& rr) {
        rr = {};
        rr.AF = static_cast<uint16_t>(h << 8);
        rr.IX = ix;
        rr.DE = de;
        return esx(e, 0x85, rr);
    };
    auto m_addr = [&](Emulator& e, uint16_t at) {
        uint32_t a = 0;
        for (int i = 0; i < 4; ++i)
            a |= static_cast<uint32_t>(e.mmu().read(static_cast<uint16_t>(at + i))) << (8 * i);
        return a;
    };
    auto m_blocks = [&](Emulator& e, uint16_t at) {
        return static_cast<uint32_t>(e.mmu().read(at) | (e.mmu().read(at + 1) << 8));
    };
    auto h_start = [&](Emulator& e, uint32_t addr, uint8_t flags, Z80Registers& rr) {
        rr = {};
        rr.AF = static_cast<uint16_t>(flags << 8);
        rr.IX = static_cast<uint16_t>(addr >> 16);
        rr.DE = static_cast<uint16_t>(addr);
        return esx(e, 0x86, rr);
    };
    auto h_end = [&](Emulator& e, Z80Registers& rr) {
        rr = {};
        rr.AF = 0x0200;
        return esx(e, 0x87, rr);
    };
    auto pin = [&](Emulator& e) { return e.port().in(0x00EB); };
    // The SPI path's steady answer: a read returns the PREVIOUS transfer's
    // byte (spi_master.vhd:162-166), so the first read after a reset is stale.
    auto spi_idle = [&](Emulator& e) { (void)pin(e); return pin(e); };
    // Map file `name` fully (one entry) and return its first block address, 0 on failure.
    auto map_first = [&](Emulator& e, const char* name, int* handle_out = nullptr) {
        const int h = h_open(e, name);
        Z80Registers rr{};
        if (handle_out) *handle_out = h;
        for (uint16_t i = 0; i < 6; ++i) e.mmu().write(static_cast<uint16_t>(0x9500 + i), 0);
        // DE=0 on exit: the one entry was written (not stale memory)
        if (h < 0 || !h_map(e, h, 0x9500, 1, rr) || carry(rr) || rr.DE != 0)
            return uint32_t{0};
        return m_addr(e, 0x9500);
    };

    {   // XNEX-45
        auto e = host_emu("host0.nex");
        Z80Registers rr{};
        const int h = e ? h_open(*e, "data/s.bin") : -1;
        const bool ok = h >= 0x04 && h <= 0x0B && h_map(*e, h, 0x9500, 4, rr);
        check("XNEX-45",
              "DISK_FILEMAP of a host-directory file: Fc=0, A=$02 (card 0, block addressing), "
              "DE=entries left, HL=IX+6n, one entry of 1301 blocks (API p.38; stream.asm:125-140)",
              h_fixtures && ok && !carry(rr) && reg_a(rr) == 0x02 && rr.DE == 3 &&
              rr.HL == 0x9506 && m_blocks(*e, 0x9504) == 1301,
              "h=" + std::to_string(h) + " A=" + std::to_string(reg_a(rr)) +
              " DE=" + std::to_string(rr.DE) + " HL=" + std::to_string(rr.HL) +
              " blocks=" + (e ? std::to_string(m_blocks(*e, 0x9504)) : "-"));
    }
    {   // XNEX-46
        auto e = host_emu("host0.nex");
        Z80Registers rr{};
        const uint32_t base = e ? map_first(*e, "data/s.bin") : 0;
        const bool started = base != 0 && h_start(*e, base, 0x02, rr) && !carry(rr) &&
                             rr.BC == 0x00EB;
        bool data = started;
        for (std::size_t k = 0; k < 512 && data; ++k) data = pin(*e) == sbyte(k);
        const uint8_t crc1 = started ? pin(*e) : 0xAA;
        const uint8_t crc2 = started ? pin(*e) : 0xAA;
        const uint8_t token = started ? pin(*e) : 0xAA;
        const uint8_t next = started ? pin(*e) : 0xAA;
        check("XNEX-46",
              "DISK_STRMSTART at a mapped address: BC=$00EB; port $EB gives bytes 0..511, a 2-byte "
              "CRC, the $FE token, then byte 512 (API p.39)",
              started && data && crc1 == 0 && crc2 == 0 && token == 0xFE && next == sbyte(512));
    }
    {   // XNEX-47
        auto e = host_emu("host0.nex");
        Z80Registers rr{};
        const uint32_t base = e ? map_first(*e, "data/s.bin") : 0;
        const bool started = base != 0 && h_start(*e, base, 0x82, rr) && !carry(rr);
        const uint8_t first = started ? pin(*e) : 0xAA;
        const uint8_t second = started ? pin(*e) : 0xAA;
        check("XNEX-47", "DISK_STRMSTART with bit 7 of A (no-wait): the first port byte is $FE, "
              "then byte 0 (API p.39, v2.01)",
              started && first == 0xFE && second == sbyte(0));
    }
    {   // XNEX-48
        auto e = host_emu("host0.nex");
        Z80Registers rr{};
        const uint32_t base = e ? map_first(*e, "data/s.bin") : 0;
        const bool started = base != 0 && h_start(*e, base + 1300, 0x02, rr) && !carry(rr);
        bool tail = started;
        for (std::size_t i = 0; i < 512 && tail; ++i) {
            const uint8_t want = i < 100 ? sbyte(1300 * 512 + i) : 0x00;
            tail = pin(*e) == want;
        }
        check("XNEX-48", "streaming the last block gives the 100 file bytes then zero padding to 512 "
              "(as XNEX-19)", tail);
    }
    {   // XNEX-49
        auto e = host_emu("host0.nex");
        Z80Registers rr{};
        const uint8_t idle = e ? spi_idle(*e) : 0xAA;
        const uint32_t base = e ? map_first(*e, "data/s.bin") : 0;
        const bool started = base != 0 && h_start(*e, base, 0x82, rr) && !carry(rr) &&
                             pin(*e) == 0xFE;
        const bool ended = started && h_end(*e, rr) && !carry(rr);
        check("XNEX-49", "DISK_STRMEND: Fc=0, and port $EB goes back to the SPI path (idle byte "
              "as before the stream) (API p.40; stream.asm:281-289)",
              ended && idle != 0xFE && spi_idle(*e) == idle, "idle=" + std::to_string(idle));
    }
    {   // XNEX-50
        auto e = host_emu("host0.nex");
        Z80Registers r1{}, r2{}, r3{};
        const int h = e ? h_open(*e, "data/big.bin") : -1;
        const bool c1 = h >= 0 && h_map(*e, h, 0x9500, 1, r1) && !carry(r1);
        const uint32_t a1 = c1 ? m_addr(*e, 0x9500) : 0;
        const uint32_t n1 = c1 ? m_blocks(*e, 0x9504) : 0;
        const bool c2 = c1 && h_map(*e, h, 0x9600, 1, r2) && !carry(r2);
        const uint32_t a2 = c2 ? m_addr(*e, 0x9600) : 0;
        const uint32_t n2 = c2 ? m_blocks(*e, 0x9604) : 0;
        const bool c3 = c2 && h_map(*e, h, 0x9700, 1, r3) && !carry(r3);
        check("XNEX-50",
              "DISK_FILEMAP continues from the previous call (DE=1 each): 65535 blocks, then the "
              "remaining 16385 at base+65535, then no entries (DE unchanged, HL=IX) (API p.38)",
              c3 && n1 == 65535 && a2 == a1 + 65535 && n2 == 81920 - 65535 &&
              r1.DE == 0 && r2.DE == 0 && r3.DE == 1 && r3.HL == 0x9700,
              "n1=" + std::to_string(n1) + " n2=" + std::to_string(n2) +
              " a1=" + std::to_string(a1) + " a2=" + std::to_string(a2) +
              " DE3=" + std::to_string(r3.DE) + " HL3=" + std::to_string(r3.HL));
    }
    {   // XNEX-51
        auto e = host_emu("host0.nex");
        Z80Registers rr{};
        const uint32_t a = e ? map_first(*e, "data/s.bin") : 0;
        const uint32_t b = e ? map_first(*e, "data/t.bin") : 0;
        const bool sb = b != 0 && h_start(*e, b, 0x82, rr) && !carry(rr) && pin(*e) == 0xFE &&
                        pin(*e) == tbyte(0) && pin(*e) == tbyte(1);
        const bool sa = a != 0 && h_start(*e, a, 0x82, rr) && !carry(rr) && pin(*e) == 0xFE &&
                        pin(*e) == sbyte(0) && pin(*e) == sbyte(1);
        check("XNEX-51", "two mapped files get different first addresses; streaming each gives "
              "its own bytes", a != 0 && b != 0 && a != b && sb && sa,
              "a=" + std::to_string(a) + " b=" + std::to_string(b));
    }
    {   // XNEX-52
        auto e = host_emu("host0.nex");
        Z80Registers rr{};
        int h = -1;
        const uint32_t a = e ? map_first(*e, "data/s.bin", &h) : 0;
        const bool closed = a != 0 && h_close(*e, h);
        const bool started = closed && h_start(*e, a, 0x82, rr) && !carry(rr) &&
                             pin(*e) == 0xFE && pin(*e) == sbyte(0) && pin(*e) == sbyte(1);
        check("XNEX-52", "a mapped address still streams after F_CLOSE of the handle "
              "(card addresses outlive the handle)", started);
    }
    {   // XNEX-53
        auto e = host_emu("host0.nex");
        Z80Registers rr{};
        int h = -1;
        const uint32_t a = e ? map_first(*e, "data/s.bin", &h) : 0;
        const bool closed = a != 0 && h_close(*e, h);
        int h2 = -1;
        const uint32_t a2 = closed ? map_first(*e, "data/s.bin", &h2) : 0;
        // a fresh open maps from the start; the same handle, mapped again, continues
        const bool again = h2 >= 0 && h_map(*e, h2, 0x9600, 1, rr) && !carry(rr);
        check("XNEX-53", "F_CLOSE + F_OPEN of a file: DISK_FILEMAP starts again at the first extent, "
              "the same address (the cursor is per open handle, API p.38); a second call on the "
              "same handle continues (no entry left)",
              a != 0 && a2 == a && again && rr.DE == 1,
              "a=" + std::to_string(a) + " a2=" + std::to_string(a2) + " DE=" + std::to_string(rr.DE));
    }
    {   // XNEX-54
        auto e = host_emu("host0.nex");
        Z80Registers rd{}, rc{}, ro{};
        bool dir_bad = false;
        if (e) {
            write_zstr(*e, 0x9100, "data");
            Z80Registers od{};
            od.AF = static_cast<uint16_t>('*' << 8);
            od.BC = 0x0000;
            od.IX = 0x9100;
            if (esx(*e, 0xA3, od) && !carry(od))
                dir_bad = h_map(*e, reg_a(od), 0x9500, 1, rd) && carry(rd) && reg_a(rd) == 0x0D;
        }
        const int h = e ? h_open(*e, "data/t.bin") : -1;
        const bool closed = h >= 0 && h_close(*e, h);
        const bool closed_bad = closed && h_map(*e, h, 0x9500, 1, rc) && carry(rc) && reg_a(rc) == 0x0D;
        const bool other = e && h_map(*e, 0x30, 0x9500, 1, ro) && carry(ro) && reg_a(ro) == 0x02;
        check("XNEX-54", "DISK_FILEMAP of a directory handle or a closed file handle: Fc=1, A=$0D "
              "(unmeasured on NextZXOS, as the extended path); a handle that is not ours: the "
              "catch-all Fc=1, A=$02, unchanged", dir_bad && closed_bad && other);
    }
    {   // XNEX-55
        auto e = host_emu("host1.nex");
        Z80Registers rr{};
        const uint8_t idle = e ? spi_idle(*e) : 0xAA;
        // the kept-open NEX file (handle 2) maps at its own base and streams its own bytes
        const bool kmap = e && h_map(*e, ExtendedNexHost::kHandle, 0x9500, 1, rr) && !carry(rr);
        const uint32_t kaddr = kmap ? m_addr(*e, 0x9500) : 0;
        const bool kstream = kmap && kaddr == ExtendedNexHost::kSyntheticFirstBlock &&
                             h_start(*e, kaddr, 0x82, rr) && !carry(rr) && pin(*e) == 0xFE &&
                             pin(*e) == 'N' && pin(*e) == 'e' && pin(*e) == 'x' && pin(*e) == 't';
        // a host-directory file, with the extended bridge also active, maps in its own
        // region; starting its stream ends the NEX file's (one card, one stream)
        const uint32_t haddr = e ? map_first(*e, "data/t.bin") : 0;
        const bool hstream = haddr != 0 && haddr != kaddr &&
                             h_start(*e, haddr, 0x82, rr) && !carry(rr) && pin(*e) == 0xFE &&
                             pin(*e) == tbyte(0) && pin(*e) == tbyte(1);
        // and starting the kept-open file's stream again ends the host one
        const bool kagain = hstream && h_start(*e, kaddr, 0x82, rr) && !carry(rr) &&
                            pin(*e) == 0xFE && pin(*e) == 'N';
        const bool kend = kagain && h_end(*e, rr) && !carry(rr);
        check("XNEX-55", "extended NEX plus host directory: DISK_FILEMAP of the kept-open handle "
              "still maps at kSyntheticFirstBlock and streams the NEX bytes; a host-directory file "
              "maps in its own region and streams its own bytes; one stream at a time, so after "
              "the last DISK_STRMEND port $EB is the SPI path",
              kstream && hstream && kagain && kend && spi_idle(*e) == idle,
              "kaddr=" + std::to_string(kaddr) + " haddr=" + std::to_string(haddr));
    }
    {   // XNEX-56
        Emulator re;
        EmulatorConfig rc = cfg;
        rc.esxdos_stub_root = hroot.string();
        rc.load_file.clear();
        const bool ok = re.init(rc);
        Z80Registers rr{};
        const bool not_claimed = ok && !h_start(re, 0x00001000, 0x02, rr);
        const bool end_not_claimed = ok && !h_end(re, rr);
        check("XNEX-56", "DISK_STRMSTART at an address outside every host region, and DISK_STRMEND "
              "with no host stream, are not claimed (bare --esxdos-stub-root, no direct NEX: "
              "dispatched to $0008, so NextZXOS's own streaming works)",
              not_claimed && end_not_claimed);
    }
    {   // XNEX-57
        auto e = host_emu("host0.nex");
        Z80Registers rr{};
        const uint8_t idle = e ? spi_idle(*e) : 0xAA;
        const uint32_t a = e ? map_first(*e, "data/s.bin") : 0;
        const bool live = a != 0 && h_start(*e, a, 0x82, rr) && !carry(rr) && pin(*e) == 0xFE;
        if (e) e->soft_reset();
        const bool soft_gone = live && !h_start(*e, a, 0x82, rr) && spi_idle(*e) == idle;
        // a new load starts a fresh table: the old address is not served
        const bool reload = soft_gone && e->load_nex((hroot / "host0.nex").string());
        const bool reload_gone = reload && h_start(*e, a, 0x82, rr) && carry(rr) &&
                                 reg_a(rr) == 0x02 && spi_idle(*e) == idle;
        // and a mapped stream does not survive the reload either
        const uint32_t b = reload_gone ? map_first(*e, "data/s.bin") : 0;
        const bool live2 = b != 0 && h_start(*e, b, 0x82, rr) && !carry(rr) && pin(*e) == 0xFE;
        const bool reload2 = live2 && e->load_nex((hroot / "host0.nex").string());
        check("XNEX-57", "soft reset and a new load_nex drop the host regions and any host stream: "
              "the old address is not served and port $EB is the SPI path again",
              soft_gone && reload_gone && reload2 && spi_idle(*e) == idle);
    }
    {   // XNEX-58: the whole game-style flow, as Z80 code
        auto e = host_emu("host0.nex");
        std::vector<uint8_t> code;
        std::vector<std::size_t> fails;
        auto b = [&](std::initializer_list<uint8_t> v) { code.insert(code.end(), v.begin(), v.end()); };
        auto jr_c_fail = [&]() { b({0x38, 0x00}); fails.push_back(code.size() - 1); };
        const uint16_t org = 0xC000, name_at = 0xC100;
        b({0xF3});                                    // DI
        b({0x3E, '*', 0x06, 0x01});                   // LD A,'*' : LD B,FA_READ
        b({0xDD, 0x21, static_cast<uint8_t>(name_at), static_cast<uint8_t>(name_at >> 8)});
        b({0xCF, 0x9A}); jr_c_fail();                 // F_OPEN
        b({0xDD, 0x21, 0x00, 0x95, 0x11, 0x02, 0x00});// LD IX,$9500 : LD DE,2
        b({0xCF, 0x85}); jr_c_fail();                 // DISK_FILEMAP
        b({0xED, 0x53, 0x10, 0x90});                  // LD ($9010),DE  (entries left)
        b({0xED, 0x5B, 0x00, 0x95});                  // LD DE,($9500)
        b({0xDD, 0x2A, 0x02, 0x95});                  // LD IX,($9502)
        b({0x3E, 0x02});                              // LD A,2
        b({0xCF, 0x86}); jr_c_fail();                 // DISK_STRMSTART -> BC=$00EB
        b({0x21, 0x00, 0xA0, 0x06, 0x00});            // LD HL,$A000 : LD B,0
        b({0xED, 0xB2, 0xED, 0xB2});                  // INIR x2 = 512 bytes
        b({0xED, 0x78, 0xED, 0x78});                  // IN A,(C) x2 = CRC
        const std::size_t wait = code.size();
        b({0xED, 0x78, 0xFE, 0xFE, 0x20, 0x00});      // wait: IN A,(C) : CP $FE : JR NZ,wait
        code[code.size() - 1] = static_cast<uint8_t>(wait - code.size());
        b({0xED, 0x78, 0x32, 0x04, 0x90});            // IN A,(C) : LD ($9004),A  (byte 512)
        b({0x3E, 0x02, 0xCF, 0x87}); jr_c_fail();     // DISK_STRMEND
        b({0x3E, 0x5A, 0x32, 0x00, 0x90, 0x76});      // marker, HALT
        const std::size_t fail_at = code.size();
        b({0x3E, 0xEE, 0x32, 0x01, 0x90, 0xF3, 0x76});// error marker : DI : HALT
        for (std::size_t at : fails)
            code[at] = static_cast<uint8_t>(fail_at - (at + 1));
        bool ok = e && code.size() < 0x100;
        if (ok) {
            for (std::size_t i = 0; i < code.size(); ++i)
                e->mmu().write(static_cast<uint16_t>(org + i), code[i]);
            write_zstr(*e, name_at, "data/s.bin");
            for (uint16_t a : {0x9000, 0x9001, 0x9004, 0x9010, 0x9011})
                e->mmu().write(a, 0);
            auto rg = e->cpu().get_registers();
            rg.PC = org; rg.SP = 0xBFF0; rg.IFF1 = 0; rg.IFF2 = 0;
            e->cpu().set_registers(rg);
            for (int f = 0; f < 3; ++f) e->run_frame();
        }
        bool bytes = ok;
        for (std::size_t k = 0; k < 512 && bytes; ++k)
            bytes = e->mmu().read(static_cast<uint16_t>(0xA000 + k)) == sbyte(k);
        check("XNEX-58", "end to end, as Z80 code: F_OPEN, DISK_FILEMAP (one entry), "
              "DISK_STRMSTART, 2 x INIR + CRC + token wait on port $EB, DISK_STRMEND: the file's "
              "bytes 0..511 and 512 arrive and the program ends on its success marker, not on "
              "the error HALT (stream.asm flow; API pp.38-40)",
              ok && e->mmu().read(0x9000) == 0x5A && e->mmu().read(0x9001) == 0 && bytes &&
              e->mmu().read(0x9004) == sbyte(512) &&
              e->mmu().read(0x9010) == 1 && e->mmu().read(0x9011) == 0,
              ok ? "marker=" + std::to_string(e->mmu().read(0x9000)) +
                   " err=" + std::to_string(e->mmu().read(0x9001)) +
                   " b512=" + std::to_string(e->mmu().read(0x9004)) : "setup");
    }

    {   // XNEX-59 (review 1, N3)
        auto e = host_emu("host1.nex");
        Z80Registers rr{};
        const uint8_t idle = e ? spi_idle(*e) : 0xAA;
        const uint32_t a = e ? map_first(*e, "data/t.bin") : 0;
        const bool live = a != 0 && h_start(*e, a, 0x82, rr) && !carry(rr) && pin(*e) == 0xFE &&
                          pin(*e) == tbyte(0);
        const bool ended = live && h_end(*e, rr) && !carry(rr);
        check("XNEX-59", "extended NEX plus host directory: DISK_STRMEND while a HOST stream runs "
              "falls through the bridge's case: Fc=0, and port $EB is the SPI path again "
              "(API p.40)", ended && spi_idle(*e) == idle);
    }
    {   // XNEX-60 (review 1, N5)
        auto e = host_emu("host0.nex");
        Z80Registers rr{};
        const uint8_t idle = e ? spi_idle(*e) : 0xAA;
        const uint32_t a = e ? map_first(*e, "data/s.bin") : 0;   // region 0
        const uint32_t b = e ? map_first(*e, "data/t.bin") : 0;   // region 1
        const bool sa = a != 0 && b != 0 && h_start(*e, a, 0x82, rr) && !carry(rr) &&
                        pin(*e) == 0xFE && pin(*e) == sbyte(0);
        // no DISK_STRMEND between: starting the second ends the first (one card)
        const bool sb = sa && h_start(*e, b, 0x82, rr) && !carry(rr) && pin(*e) == 0xFE &&
                        pin(*e) == tbyte(0) && pin(*e) == tbyte(1);
        const bool ended = sb && h_end(*e, rr) && !carry(rr);
        check("XNEX-60", "a host stream started while another host stream runs replaces it "
              "(region 1's bytes, not region 0's), and one DISK_STRMEND leaves port $EB on the "
              "SPI path (stream.asm:174-200, :281-289; one CMD18 on a real card)",
              ended && spi_idle(*e) == idle);
    }
    {   // XNEX-61 (review 1, N11/N34)
        const auto many = hroot / "many";
        std::filesystem::create_directories(many, ec);
        constexpr int kFiles = 225;
        for (int i = 0; i < kFiles; ++i) {
            char nm[16];
            std::snprintf(nm, sizeof nm, "f%03d.bin", i);
            std::ofstream(many / nm, std::ios::binary).put('x');
        }
        auto e = host_emu("host0.nex");
        auto name_of = [](int i) {
            char nm[32];
            std::snprintf(nm, sizeof nm, "many/f%03d.bin", i);
            return std::string(nm);
        };
        bool ok = static_cast<bool>(e);
        int bad = -1;
        for (int i = 0; ok && i < 224; ++i) {
            const uint32_t a = map_first(*e, name_of(i).c_str());
            const uint32_t want = HostFileStreams::kFirstBlock +
                                  static_cast<uint32_t>(i) * HostFileStreams::kRegionBlocks;
            // closing frees the handle slot; the region stays
            if (a != want || !h_close(*e, 4)) { ok = false; bad = i; }
        }
        Z80Registers rr{};
        const int h = ok ? h_open(*e, name_of(224).c_str()) : -1;
        const bool full = h >= 0 && h_map(*e, h, 0x9500, 1, rr) && carry(rr) && reg_a(rr) == 0x0C;
        const bool closed = full && h_close(*e, h);
        const uint32_t again = closed ? map_first(*e, name_of(7).c_str()) : 0;
        check("XNEX-61", "224 distinct files map (regions 0..223 at 0x90000000 + i*0x00800000); the "
              "225th gets Fc=1, A=$0C (esx_enfile) instead of wrapping onto real card "
              "addresses; an already-mapped file still maps when the table is full",
              ok && full && again == HostFileStreams::kFirstBlock +
                                         7u * HostFileStreams::kRegionBlocks,
              "bad=" + std::to_string(bad) + " full=" + std::to_string(full));
    }
    {   // XNEX-62 (review 1, N33)
        auto e = std::make_unique<Emulator>();
        EmulatorConfig bc = cfg;
        bc.esxdos_stub = false;   // the bridge alone: no host directory
        bc.load_file = (hroot / "host1.nex").string();
        const bool ok = e->init(bc) && e->load_nex(bc.load_file);
        Z80Registers rr{};
        const bool bad = ok && h_map(*e, 0x04, 0x9500, 1, rr) && carry(rr) && reg_a(rr) == 0x0D;
        check("XNEX-62", "bridge NEX with no host directory: DISK_FILEMAP of a handle in $04..$0B "
              "is still the bridge's own esx_ebadf (Fc=1, A=$0D), not the catch-all", bad,
              "A=" + std::to_string(reg_a(rr)));
    }

    Log::emulator()->sinks().pop_back();

    std::filesystem::remove_all(root, ec);
    std::printf("\nTotal:%4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                total, passed, failed, 0);
    return failed == 0 ? 0 : 1;
}
