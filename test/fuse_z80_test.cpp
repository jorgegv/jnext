// FUSE Z80 Opcode Test Runner
//
// Parses FUSE's tests.in / tests.expected files and runs each test case
// against our Z80 CPU implementation. Compares final register and memory
// state (ignores bus cycle events — we only validate correctness, not timing).

#include "cpu/z80_cpu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

// Simple 64K RAM for test isolation
class TestMemory : public MemoryInterface {
public:
    uint8_t ram[65536];
    TestMemory() { memset(ram, 0, sizeof(ram)); }
    uint8_t read(uint16_t addr) override { return ram[addr]; }
    void write(uint16_t addr, uint8_t val) override { ram[addr] = val; }
};

class TestIO : public IoInterface {
public:
    // FUSE tests expect IN to return the high byte of the port address
    // (simulating floating bus / no device responding — the port address
    // is reflected back on the data bus).
    uint8_t in(uint16_t port) override { return static_cast<uint8_t>(port >> 8); }
    void out(uint16_t, uint8_t) override {}
};

struct TestCase {
    std::string name;
    // Initial/expected register state
    uint16_t AF, BC, DE, HL;
    uint16_t AF2, BC2, DE2, HL2;
    uint16_t IX, IY, SP, PC, MEMPTR;
    uint8_t  I, R;
    unsigned IFF1, IFF2, IM;
    int      halted;
    int      tstates;
    // Memory blocks: each is (address, bytes...)
    struct MemBlock {
        uint16_t addr;
        std::vector<uint8_t> data;
    };
    std::vector<MemBlock> mem_blocks;
};

static bool parse_test_input(std::ifstream& f, TestCase& tc) {
    std::string line;
    // Read test name
    while (std::getline(f, line)) {
        // Skip blank lines
        if (line.empty() || line[0] == '\n' || line[0] == '\r') continue;
        tc.name = line;
        // Trim trailing whitespace
        while (!tc.name.empty() && (tc.name.back() == '\r' || tc.name.back() == ' '))
            tc.name.pop_back();
        break;
    }
    if (f.eof()) return false;

    // Read registers: AF BC DE HL AF' BC' DE' HL' IX IY SP PC MEMPTR
    if (!std::getline(f, line)) return false;
    std::istringstream iss(line);
    iss >> std::hex >> tc.AF >> tc.BC >> tc.DE >> tc.HL
        >> tc.AF2 >> tc.BC2 >> tc.DE2 >> tc.HL2
        >> tc.IX >> tc.IY >> tc.SP >> tc.PC >> tc.MEMPTR;

    // Read I R IFF1 IFF2 IM halted tstates
    if (!std::getline(f, line)) return false;
    {
        std::istringstream iss2(line);
        unsigned i_val, r_val, iff1, iff2, im;
        iss2 >> std::hex >> i_val >> r_val >> iff1 >> iff2 >> im >> tc.halted >> std::dec >> tc.tstates;
        tc.I = static_cast<uint8_t>(i_val);
        tc.R = static_cast<uint8_t>(r_val);
        tc.IFF1 = iff1; tc.IFF2 = iff2; tc.IM = im;
    }

    // Read memory blocks until -1
    tc.mem_blocks.clear();
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '\n' || line[0] == '\r') continue;
        std::istringstream iss3(line);
        int addr;
        iss3 >> std::hex >> addr;
        if (addr == -1) break;
        TestCase::MemBlock mb;
        mb.addr = static_cast<uint16_t>(addr);
        int byte_val;
        while (iss3 >> std::hex >> byte_val) {
            if (byte_val == -1) break;
            mb.data.push_back(static_cast<uint8_t>(byte_val));
        }
        tc.mem_blocks.push_back(mb);
    }
    return true;
}

static bool parse_test_expected(std::ifstream& f, TestCase& tc) {
    std::string line;
    // Read test name (skip blank lines)
    while (std::getline(f, line)) {
        // Trim trailing CR
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        tc.name = line;
        break;
    }
    if (f.eof()) return false;

    // Collect all remaining lines of this test block (until blank line or EOF)
    std::vector<std::string> block;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) break;
        block.push_back(line);
    }

    // Find the register line: first line NOT starting with whitespace
    // (bus event lines start with spaces, e.g. "    0 MC 0000")
    int reg_idx = -1;
    for (int i = 0; i < static_cast<int>(block.size()); ++i) {
        if (!block[i].empty() && block[i][0] != ' ' && block[i][0] != '\t') {
            reg_idx = i;
            break;
        }
    }
    if (reg_idx < 0 || reg_idx + 1 >= static_cast<int>(block.size())) return false;

    // Parse registers
    {
        std::istringstream iss(block[reg_idx]);
        iss >> std::hex >> tc.AF >> tc.BC >> tc.DE >> tc.HL
            >> tc.AF2 >> tc.BC2 >> tc.DE2 >> tc.HL2
            >> tc.IX >> tc.IY >> tc.SP >> tc.PC >> tc.MEMPTR;
    }

    // Parse I R IFF1 IFF2 IM halted tstates
    {
        std::istringstream iss(block[reg_idx + 1]);
        unsigned i_val, r_val, iff1, iff2, im;
        iss >> std::hex >> i_val >> r_val >> iff1 >> iff2 >> im >> tc.halted >> std::dec >> tc.tstates;
        tc.I = static_cast<uint8_t>(i_val);
        tc.R = static_cast<uint8_t>(r_val);
        tc.IFF1 = iff1; tc.IFF2 = iff2; tc.IM = im;
    }

    // Parse memory blocks (lines after register+flags line, until -1)
    tc.mem_blocks.clear();
    for (int i = reg_idx + 2; i < static_cast<int>(block.size()); ++i) {
        std::istringstream iss(block[i]);
        int addr;
        iss >> std::hex >> addr;
        if (addr == -1) break;
        TestCase::MemBlock mb;
        mb.addr = static_cast<uint16_t>(addr);
        int byte_val;
        while (iss >> std::hex >> byte_val) {
            if (byte_val == -1) break;
            mb.data.push_back(static_cast<uint8_t>(byte_val));
        }
        tc.mem_blocks.push_back(mb);
    }
    return true;
}

static void apply_state(Z80Cpu& cpu, TestMemory& mem, const TestCase& tc) {
    Z80Registers r{};
    r.AF = tc.AF; r.BC = tc.BC; r.DE = tc.DE; r.HL = tc.HL;
    r.AF2 = tc.AF2; r.BC2 = tc.BC2; r.DE2 = tc.DE2; r.HL2 = tc.HL2;
    r.IX = tc.IX; r.IY = tc.IY; r.SP = tc.SP; r.PC = tc.PC;
    r.MEMPTR = tc.MEMPTR;
    r.I = tc.I; r.R = tc.R;
    r.IFF1 = static_cast<uint8_t>(tc.IFF1);
    r.IFF2 = static_cast<uint8_t>(tc.IFF2);
    r.IM = static_cast<uint8_t>(tc.IM);
    r.Q = 0;  // Q is internal, not in test format — always starts at 0
    r.halted = tc.halted != 0;
    cpu.set_registers(r);

    // FUSE coretest always starts tstates at 0.  The "tstates" field in the
    // input file is actually event_next_event (not the initial counter).
    *fuse_z80_tstates_ptr() = 0;

    memset(mem.ram, 0, sizeof(mem.ram));
    for (auto& mb : tc.mem_blocks) {
        for (size_t i = 0; i < mb.data.size(); ++i) {
            mem.ram[(mb.addr + i) & 0xFFFF] = mb.data[i];
        }
    }
}

struct RegDiff {
    const char* name;
    unsigned got, expected;
};

static bool compare_state(const Z80Cpu& cpu, const TestMemory& mem,
                           const TestCase& expected, std::vector<RegDiff>& diffs,
                           std::vector<std::string>& mem_diffs) {
    auto r = cpu.get_registers();
    bool ok = true;

    auto chk = [&](const char* name, unsigned got, unsigned exp) {
        if (got != exp) {
            diffs.push_back({name, got, exp});
            ok = false;
        }
    };

    chk("AF", r.AF, expected.AF);
    chk("BC", r.BC, expected.BC);
    chk("DE", r.DE, expected.DE);
    chk("HL", r.HL, expected.HL);
    chk("AF'", r.AF2, expected.AF2);
    chk("BC'", r.BC2, expected.BC2);
    chk("DE'", r.DE2, expected.DE2);
    chk("HL'", r.HL2, expected.HL2);
    chk("IX", r.IX, expected.IX);
    chk("IY", r.IY, expected.IY);
    chk("SP", r.SP, expected.SP);
    chk("PC", r.PC, expected.PC);
    chk("MEMPTR", r.MEMPTR, expected.MEMPTR);
    chk("I", r.I, expected.I);
    chk("R", r.R, expected.R);
    chk("IFF1", r.IFF1, expected.IFF1);
    chk("IFF2", r.IFF2, expected.IFF2);
    chk("IM", r.IM, expected.IM);
    chk("halted", r.halted ? 1u : 0u, static_cast<unsigned>(expected.halted));
    chk("tstates", static_cast<unsigned>(*fuse_z80_tstates_ptr()),
        static_cast<unsigned>(expected.tstates));

    // Check expected memory
    for (auto& mb : expected.mem_blocks) {
        for (size_t i = 0; i < mb.data.size(); ++i) {
            uint16_t addr = (mb.addr + i) & 0xFFFF;
            uint8_t got = mem.ram[addr];
            uint8_t exp = mb.data[i];
            if (got != exp) {
                char buf[128];
                snprintf(buf, sizeof(buf), "mem[%04X]: got %02X, expected %02X", addr, got, exp);
                mem_diffs.push_back(buf);
                ok = false;
            }
        }
    }
    return ok;
}

int main(int argc, char* argv[]) {
    std::string dir = "test/fuse";
    if (argc > 1) dir = argv[1];

    std::ifstream fin(dir + "/tests.in");
    std::ifstream fexp(dir + "/tests.expected");
    if (!fin.is_open() || !fexp.is_open()) {
        fprintf(stderr, "Cannot open test files in '%s'\n", dir.c_str());
        return 1;
    }

    // Parse all input and expected test cases
    std::vector<TestCase> inputs, expected;
    {
        TestCase tc;
        while (parse_test_input(fin, tc)) inputs.push_back(tc);
        while (parse_test_expected(fexp, tc)) expected.push_back(tc);
    }

    // Build name→expected map
    std::unordered_map<std::string, const TestCase*> exp_map;
    for (auto& e : expected) exp_map[e.name] = &e;

    int total = 0, passed = 0, failed = 0, skipped = 0;
    std::vector<std::string> failures;

    TestMemory mem;
    TestIO io;
    Z80Cpu cpu(mem, io);

    for (auto& input : inputs) {
        total++;
        auto it = exp_map.find(input.name);
        if (it == exp_map.end()) {
            skipped++;
            continue;
        }
        const TestCase& exp = *it->second;

        // Set up initial state
        apply_state(cpu, mem, input);

        // Execute instruction(s). Most tests need one execute() call.
        // Repeating block instructions (LDIR, LDDR, CPIR, CPDR, INIR, INDR,
        // OTIR, OTDR) need multiple executions until they complete.
        // These are ED B0/B1/B2/B3/B8/B9/BA/BB.
        bool is_repeating = false;
        if (!input.mem_blocks.empty()) {
            uint16_t pc = input.PC;
            uint8_t op0 = 0, op1 = 0;
            for (auto& mb : input.mem_blocks) {
                if (mb.addr <= pc && pc < mb.addr + mb.data.size())
                    op0 = mb.data[pc - mb.addr];
                if (mb.addr <= pc+1 && pc+1 < mb.addr + mb.data.size())
                    op1 = mb.data[pc+1 - mb.addr];
            }
            is_repeating = (op0 == 0xED && (op1 == 0xB0 || op1 == 0xB1 ||
                op1 == 0xB2 || op1 == 0xB3 || op1 == 0xB8 || op1 == 0xB9 ||
                op1 == 0xBA || op1 == 0xBB));
        }

        if (is_repeating && exp.PC != input.PC) {
            // Repeating block that runs to completion (expected PC != start PC).
            uint16_t start_pc = input.PC;
            int safety = 10000;
            do {
                cpu.execute();
            } while (cpu.get_registers().PC == start_pc && --safety > 0);
        } else {
            // Single instruction (or repeating block testing one iteration)
            cpu.execute();
        }

        // Compare
        std::vector<RegDiff> reg_diffs;
        std::vector<std::string> mem_diffs_list;
        if (compare_state(cpu, mem, exp, reg_diffs, mem_diffs_list)) {
            passed++;
        } else {
            failed++;
            std::string msg = input.name + ": FAIL";
            for (auto& d : reg_diffs) {
                char buf[64];
                snprintf(buf, sizeof(buf), " %s=%04X(exp %04X)", d.name, d.got, d.expected);
                msg += buf;
            }
            for (auto& m : mem_diffs_list) {
                msg += " " + m;
            }
            failures.push_back(msg);
        }
    }

    // ── Multi-instruction tests ─��────────────────────────────────────
    // These tests contain instruction sequences that the single-execute()
    // runner above can't handle.  We run them separately with a loop that
    // calls execute() repeatedly until the expected PC is reached.
    static const char* multi_tests[] = { "10", "dd00", "ddfd00" };
    for (const char* name : multi_tests) {
        // Find input and expected for this test
        const TestCase* inp = nullptr;
        const TestCase* ex  = nullptr;
        for (auto& t : inputs)   if (t.name == name) { inp = &t; break; }
        auto eit = exp_map.find(name);
        if (eit != exp_map.end()) ex = eit->second;
        if (!inp || !ex) continue;

        // Remove from failures list (the main loop already counted it)
        for (auto it = failures.begin(); it != failures.end(); ++it) {
            if (it->substr(0, strlen(name) + 1) == std::string(name) + ":") {
                failures.erase(it);
                failed--;
                break;
            }
        }

        apply_state(cpu, mem, *inp);

        int safety = 100000;
        while (cpu.get_registers().PC != ex->PC && --safety > 0) {
            cpu.execute();
        }

        std::vector<RegDiff> reg_diffs;
        std::vector<std::string> mem_diffs_list;
        if (compare_state(cpu, mem, *ex, reg_diffs, mem_diffs_list)) {
            passed++;
        } else {
            failed++;
            std::string msg = std::string(name) + ": FAIL";
            for (auto& d : reg_diffs) {
                char buf[64];
                snprintf(buf, sizeof(buf), " %s=%04X(exp %04X)", d.name, d.got, d.expected);
                msg += buf;
            }
            for (auto& m : mem_diffs_list) {
                msg += " " + m;
            }
            failures.push_back(msg);
        }
    }

    // Summary
    printf("\nFUSE Z80 Test Results\n");
    printf("=====================\n");
    printf("Total: %d  Passed: %d  Failed: %d  Skipped: %d\n\n",
           total, passed, failed, skipped);

    if (!failures.empty()) {
        printf("Failures (%d):\n", static_cast<int>(failures.size()));
        for (auto& f : failures) {
            printf("  %s\n", f.c_str());
        }
    }

    return failed > 0 ? 1 : 0;
}
