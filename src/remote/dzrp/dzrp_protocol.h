#pragma once

// ---------------------------------------------------------------------------
// DZRP protocol constants shared by the server (dzrp_server.h) and the client
// (dzrp_client.h): command ids, the notification ids, the break reasons and the
// version jnext speaks. Header only. It exists so the client does not include
// the server's header, which drags in the debugger backend.
// Spec: DeZog `design/DeZogProtocol.md` (DZRP 2.2.0).
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

namespace jnext {
namespace remote {
namespace dzrp {

/// Command ids (DZRP 2.2.0, `DeZogProtocol.md`), including the two 2.2.0
/// removed and the ones jnext reports unsupported.
enum CommandId : std::uint8_t {
    CMD_INIT                                = 1,
    CMD_CLOSE                               = 2,
    CMD_GET_REGISTERS                       = 3,
    CMD_SET_REGISTER                        = 4,
    CMD_WRITE_BANK                          = 5,   // removed in 2.2.0; legacy
    CMD_CONTINUE                            = 6,
    CMD_PAUSE                               = 7,
    CMD_READ_MEM                            = 8,
    CMD_WRITE_MEM                           = 9,
    CMD_SET_SLOT                            = 10,
    CMD_GET_TBBLUE_REG                      = 11,
    CMD_SET_BORDER                          = 12,  // removed in 2.2.0; legacy
    CMD_SET_BREAKPOINTS                     = 13,
    CMD_RESTORE_MEM                         = 14,
    CMD_LOOPBACK                            = 15,
    CMD_GET_SPRITES_PALETTE                 = 16,
    CMD_GET_SPRITES_CLIP_WINDOW_AND_CONTROL = 17,
    CMD_GET_SPRITES                         = 18,
    CMD_GET_SPRITE_PATTERNS                 = 19,
    CMD_READ_PORT                           = 20,
    CMD_WRITE_PORT                          = 21,
    CMD_EXEC_ASM                            = 22,
    CMD_INTERRUPT_ON_OFF                    = 23,
    CMD_GET_SUPPORTED_COMMANDS              = 24,
    CMD_READ_BANK_MEM                       = 25,
    CMD_WRITE_BANK_MEM                      = 26,
    CMD_SET_NEXTREGS                        = 27,
    CMD_READ_MEM_BLOCKS                     = 28,
    CMD_ENABLE_BREAK_ON_INTERRUPT           = 39,
    CMD_ADD_BREAKPOINT                      = 40,
    CMD_REMOVE_BREAKPOINT                   = 41,
    CMD_ADD_WATCHPOINT                      = 42,
    CMD_REMOVE_WATCHPOINT                   = 43,
    CMD_READ_STATE                          = 50,
    CMD_WRITE_STATE                         = 51,
};

/// The DZRP version jnext answers `CMD_INIT` with (design F1): 2.2.0 satisfies
/// DeZog 3.8 (`DZRP_VERSION [2,2,0]`) and, by DeZog's "major equal, remote minor
/// >= client minor" rule, the marketplace 3.7.4 (`[2,0,0]`) as well.
constexpr std::uint8_t DZRP_VERSION_MAJOR = 2;
constexpr std::uint8_t DZRP_VERSION_MINOR = 2;
constexpr std::uint8_t DZRP_VERSION_PATCH = 0;

/// `CMD_INIT`'s machine type: ZXNEXT, for every `--machine` (design §5.1 - the
/// MMU is the Next's in every mode; CSpect "will always return ZXNEXT" too).
constexpr std::uint8_t DZRP_MACHINE_ZXNEXT = 4;

/// Bytes in one DZRP bank = one MMU page (design §5.2).
constexpr std::uint32_t DZRP_BANK_BYTES = 0x2000;

/// DeZog 3.8's ROM bank id in `CMD_READ/WRITE_BANK_MEM`: ONE 16 KB bank whose
/// halves are slot 0 and slot 1 (`zxnextmemorymodels.ts:64-104`).
constexpr std::uint8_t DZRP_ROM_BANK = 0xFF;

/// `NTF_PAUSE`, the notification jnext sends, and `NTF_LOG`, which a remote may send (jnext does not).
constexpr std::uint8_t NTF_PAUSE = 1;
constexpr std::uint8_t NTF_LOG   = 2;

/// `NTF_PAUSE` break reasons (spec: 0 no reason / step, 1 manual break,
/// 2 breakpoint, 3 watchpoint read, 4 watchpoint write, 255 other + string).
enum BreakReason : std::uint8_t {
    BREAK_NONE      = 0,
    BREAK_MANUAL    = 1,
    BREAK_BP        = 2,
    BREAK_WP_READ   = 3,
    BREAK_WP_WRITE  = 4,
    BREAK_OTHER     = 255,
};

}  // namespace dzrp
}  // namespace remote
}  // namespace jnext
