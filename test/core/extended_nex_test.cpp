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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int passed = 0;
int failed = 0;
int total = 0;

void check(const char* id, const char* desc, bool condition,
           const std::string& detail = {}) {
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
               const std::vector<uint8_t>& appended) {
    constexpr std::size_t bank_size = 16384;
    std::vector<uint8_t> bytes(512 + bank_size + appended.size(), 0);
    std::memcpy(bytes.data(), "NextV1.2", 8);
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
    gui_emu.reset();
    regs = {};
    const bool hard_bridge_gone = !esx(gui_emu, 0x88, regs);
    check("XNEX-26", "hard reset disarms a reloaded host bridge",
          rearmed_after_soft && hard_bridge_gone &&
          !gui_emu.sd_card().has_read_overlay());

    // A command-line load reattaches the dormant hook during reset because
    // config_.load_file remains a NEX path. The hook must nevertheless decline
    // the call once reset has cleared the host path, allowing the real ROM's
    // RST $08 handler to run.
    Emulator cli_emu;
    EmulatorConfig cli_cfg = cfg;
    const bool cli_loaded =
        cli_emu.init(cli_cfg) &&
        cli_emu.load_nex(register_path.string());
    cli_emu.reset();
    regs = {};
    const bool cli_hook_present =
        static_cast<bool>(cli_emu.cpu().on_esxdos_call);
    const bool cli_bridge_gone = !esx(cli_emu, 0x88, regs);
    check("XNEX-27", "CLI reset keeps a dormant hook without servicing host calls",
          cli_loaded && cli_hook_present && cli_bridge_gone &&
          !cli_emu.sd_card().has_read_overlay());

    std::filesystem::remove_all(root, ec);
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                total, passed, failed, 0);
    return failed == 0 ? 0 : 1;
}
