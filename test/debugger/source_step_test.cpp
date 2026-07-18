#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/breakpoints.h"
#include "debug/debug_state.h"
#include "debug/sld_loader.h"
#include "debugger/debugger_manager.h"

#include <QApplication>
#include <QMainWindow>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace {

int passed = 0;
int failed = 0;

void check(const char* name, bool condition)
{
    if (condition) {
        ++passed;
    } else {
        ++failed;
        std::cout << "FAIL: " << name << '\n';
    }
}

void set_pc(Emulator& emulator, uint16_t pc)
{
    auto registers = emulator.cpu().get_registers();
    registers.PC = pc;
    registers.SP = 0xFF00;
    registers.IFF1 = 0;
    registers.IFF2 = 0;
    registers.halted = false;
    emulator.cpu().set_registers(registers);
    emulator.call_stack().clear();
    emulator.debug_state().set_data_bp_hit(false);
    emulator.debug_state().pause();
}

void write_program(Emulator& emulator)
{
    emulator.mmu().write(0x8000, 0x00); // line 1: NOP
    emulator.mmu().write(0x8001, 0x00); // line 1: NOP
    emulator.mmu().write(0x8002, 0x00); // line 2: NOP

    emulator.mmu().write(0x8010, 0xCD); // line 3: CALL $8020
    emulator.mmu().write(0x8011, 0x20);
    emulator.mmu().write(0x8012, 0x80);
    emulator.mmu().write(0x8013, 0x00); // line 4: NOP
    emulator.mmu().write(0x8020, 0x00); // line 10: NOP
    emulator.mmu().write(0x8021, 0xC9); // line 11: RET

    emulator.mmu().write(0x8030, 0x32); // line 20: LD ($9000),A
    emulator.mmu().write(0x8031, 0x00);
    emulator.mmu().write(0x8032, 0x90);
    emulator.mmu().write(0x8033, 0x00); // line 21: NOP

    emulator.mmu().write(0x8040, 0x00); // line 30: NOP
    emulator.mmu().write(0x8041, 0x00); // line 31: NOP
    emulator.mmu().write(0x8042, 0xC3); // line 32: JP $8040
    emulator.mmu().write(0x8043, 0x40);
    emulator.mmu().write(0x8044, 0x80);

    emulator.mmu().write(0x8050, 0x00); // line 40: NOP
    emulator.mmu().write(0x8051, 0x00); // compiler-generated, unmapped
    emulator.mmu().write(0x8052, 0x00); // line 41: NOP
}

fs::path write_source_map(uint8_t page)
{
    const auto stamp = std::chrono::high_resolution_clock::now()
                           .time_since_epoch().count();
    const fs::path directory =
        fs::temp_directory_path() /
        ("jnext-source-step-" + std::to_string(stamp));
    fs::create_directories(directory);
    const fs::path path = directory / "step.sld";
    std::ofstream output(path);
    output << "|SLD.data.version|1\n"
           << "main.bas|1||0|-1|-1|Z|pages.size:8192,pages.count:224,"
              "slots.count:8,slots.adr:0,8192,16384,24576,32768,40960,"
              "49152,57344\n";
    for (const auto [line, address] : {
             std::pair{1, 0x8000}, std::pair{1, 0x8001},
             std::pair{2, 0x8002}, std::pair{3, 0x8010},
             std::pair{4, 0x8013}, std::pair{10, 0x8020},
             std::pair{11, 0x8021}, std::pair{20, 0x8030},
             std::pair{21, 0x8033}, std::pair{30, 0x8040},
             std::pair{31, 0x8041}, std::pair{32, 0x8042},
             std::pair{40, 0x8050}, std::pair{41, 0x8052}}) {
        output << "main.bas|" << line << "||0|" << static_cast<int>(page)
               << '|' << address << "|T|\n";
    }
    return path;
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    Emulator emulator;
    EmulatorConfig config;
    config.type = MachineType::ZX48K;
    config.rewind_buffer_frames = 4;
    if (!emulator.init(config)) {
        std::cout << "FAIL: initializes test emulator\n";
        return 1;
    }
    write_program(emulator);

    QMainWindow window;
    DebuggerManager manager(&window, &emulator, &window);
    check("enables debugger", manager.set_enabled(true));
    const uint8_t page = emulator.mmu().get_effective_page(4);
    const fs::path step_map = write_source_map(page);
    check("loads source-step map",
          load_sld(manager.source_map(), step_map.string()).count == 14);

    const uint8_t other_page = static_cast<uint8_t>(page ^ 1);
    auto& breakpoints = emulator.debug_state().breakpoints();
    breakpoints.add_pc(other_page, 0x8000);
    check("banked breakpoint does not match another physical page",
          !breakpoints.has_pc(page, 0x8000));
    check("banked breakpoint matches its physical page",
          breakpoints.has_pc(other_page, 0x8000));
    check("debug state ignores same logical PC on another page",
          !emulator.debug_state().should_break(page, 0x8000));
    check("debug state breaks on matching page and logical PC",
          emulator.debug_state().should_break(other_page, 0x8000));
    breakpoints.remove_pc(other_page, 0x8000);

    set_pc(emulator, 0x8040);
    breakpoints.add_pc(other_page, 0x8040);
    emulator.debug_state().resume();
    emulator.run_frame();
    check("execution ignores banked breakpoint on another page",
          !emulator.debug_state().paused());
    breakpoints.remove_pc(other_page, 0x8040);
    breakpoints.add_pc(page, 0x8040);
    emulator.run_frame();
    check("execution stops on matching banked breakpoint",
          emulator.debug_state().paused()
              && emulator.cpu().get_registers().PC == 0x8040);
    breakpoints.clear_all_pc();

    set_pc(emulator, 0x8000);
    manager.on_source_step_into();
    check("step into skips instructions on the same source line",
          emulator.cpu().get_registers().PC == 0x8002);

    set_pc(emulator, 0x8000);
    breakpoints.set_oneshot(0x8001);
    manager.on_source_step_into();
    check("source step honors one-shot breakpoint on the same source line",
          emulator.cpu().get_registers().PC == 0x8001);
    breakpoints.clear_oneshot();

    set_pc(emulator, 0x8050);
    manager.on_source_step_into();
    check("source step crosses unmapped compiler-generated code",
          emulator.cpu().get_registers().PC == 0x8052);

    set_pc(emulator, 0x8010);
    manager.on_source_step_into();
    check("step into enters a mapped call",
          emulator.cpu().get_registers().PC == 0x8020);
    check("step into tracks call depth", emulator.call_stack().frames().size() == 1);
    check("call stack records caller and target physical pages",
          emulator.call_stack().frames().size() == 1
              && emulator.call_stack().frames().back().caller_page == page
              && emulator.call_stack().frames().back().target_page == page);

    set_pc(emulator, 0x8010);
    manager.on_source_step_over();
    check("step over stops after the mapped call returns",
          emulator.cpu().get_registers().PC == 0x8013);
    check("step over restores call depth", emulator.call_stack().frames().empty());

    set_pc(emulator, 0x8010);
    emulator.execute_single_instruction();
    emulator.debug_state().pause();
    check("out fixture enters function", emulator.cpu().get_registers().PC == 0x8020);
    check("out fixture has a call frame", emulator.call_stack().frames().size() == 1);
    manager.on_source_step_out();
    check("step out stops in the mapped caller",
          emulator.cpu().get_registers().PC == 0x8013);
    check("step out removes call frame", emulator.call_stack().frames().empty());

    set_pc(emulator, 0x8030);
    emulator.debug_state().breakpoints().add_watchpoint(0x9000, WatchType::WRITE);
    manager.on_source_step_into();
    check("source step stops after watched write",
          emulator.cpu().get_registers().PC == 0x8033);
    check("source step clears data-breakpoint latch",
          !emulator.debug_state().data_bp_hit());

    emulator.debug_state().breakpoints().clear_all_watchpoints();
    emulator.trace_log().clear();
    set_pc(emulator, 0x8040);
    emulator.call_stack().restore_frames({
        {page, 0x7000, page, 0x8040, 0xFEFE, CallType::CALL}
    });
    emulator.debug_state().resume();
    emulator.run_frame();
    emulator.run_frame();
    emulator.debug_state().pause();
    const uint16_t before_pc = emulator.cpu().get_registers().PC;
    const auto before = manager.source_map().lookup(page, before_pc);
    check("reverse fixture stops on mapped source", before.has_value());
    manager.on_source_step_back();
    const uint16_t after_pc = emulator.cpu().get_registers().PC;
    const auto after = manager.source_map().lookup(page, after_pc);
    check("source step back lands on mapped source", after.has_value());
    check("source step back changes source line",
          before && after && before->line != after->line);
    check("source step back restores call-stack history",
          emulator.call_stack().frames().size() == 1
              && emulator.call_stack().frames().back().caller_pc == 0x7000);

    emulator.debug_state().breakpoints().add_pc(page, 0x8040);
    manager.on_source_reverse_continue();
    check("reverse continue lands on source breakpoint",
          emulator.cpu().get_registers().PC == 0x8040);

    const fs::path first_map = write_source_map(page);
    const fs::path first_program = first_map.parent_path() / "step.nex";
    {
        std::ofstream memory(first_map.parent_path() / "step.Memory.txt");
        memory << "8000: ._First\n";
    }
    manager.load_debug_sidecars_for_program(first_program.string());
    check("program sidecars load symbols", manager.symbol_table().resolve("First") == 0x8000);
    check("program sidecars load source", manager.source_map().loaded_file() == first_map.string());

    const fs::path second_map = write_source_map(page);
    const fs::path second_program = second_map.parent_path() / "step.nex";
    {
        std::ofstream memory(second_map.parent_path() / "step.Memory.txt");
        memory << "8010: ._Second\n";
    }
    manager.load_debug_sidecars_for_program(second_program.string());
    check("new program replaces symbols",
          manager.symbol_table().resolve("Second") == 0x8010
              && !manager.symbol_table().resolve("First"));
    check("new program replaces source map",
          manager.source_map().loaded_file() == second_map.string());

    manager.load_debug_sidecars_for_program(
        (second_map.parent_path() / "missing.nex").string());
    check("missing program sidecars clear symbols", manager.symbol_table().empty());
    check("missing program sidecars clear source", manager.source_map().empty());

    manager.set_enabled(false, false);
    fs::remove_all(step_map.parent_path());
    fs::remove_all(first_map.parent_path());
    fs::remove_all(second_map.parent_path());
    std::cout << "Total: " << passed + failed << "  Passed: " << passed
              << "  Failed: " << failed << "  Skipped: 0\n";
    return failed == 0 ? 0 : 1;
}
