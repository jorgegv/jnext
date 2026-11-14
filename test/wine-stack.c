/* wine-stack: name the call each thread of ONE Windows process is blocked in, without a debugger.
 *
 * Test tool of the regression harness (test/wine-run.sh --capture, GH #383 / #487); never bundled
 * into a package. Built twice by `make wine-stack-x64` / `wine-stack-x86` from this one file:
 * the helper's bitness must match the target's (an x64 helper on a WoW64 target finds the process
 * but loses the 32-bit frames; measured on wine 11.20).
 *
 * Usage: wine-stack.exe NAME=VALUE exe-base-name
 *   Finds the process whose image name is exe-base-name AND whose environment holds the entry
 *   NAME=VALUE exactly (the runner's per-run marker: parallel rows share a prefix and an exe name).
 *   Exactly one match: prints "match pid <hex> <name>", "command line: <text>", then per thread
 *     Backtracing for thread <tid> in process <pid>:
 *     Backtrace:
 *       <n> 0x<pc> <symbol>+0x<off> in <module>
 *   Otherwise prints "matches=<n>" and exits 1.
 * No debugger is attached (winedbg's attach kills a 32-bit process on wine 11.20): a thread is
 * suspended only around GetThreadContext, so a helper killed by its bound never leaves one stopped.
 */
#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <dbghelp.h>
#include <psapi.h>
#include <stdio.h>
#include <wchar.h>

#ifdef _WIN64
#define PEB_PARAMS 0x20
#define PARAMS_ENV 0x80
#define PARAMS_CMDLINE 0x70
#define PTR_SIZE 8
#else
#define PEB_PARAMS 0x10
#define PARAMS_ENV 0x48
#define PARAMS_CMDLINE 0x40
#define PTR_SIZE 4
#endif

static int read_ptr(HANDLE h, ULONG_PTR addr, ULONG_PTR *out)
{
    SIZE_T got;
    *out = 0;
    return ReadProcessMemory(h, (void *)addr, out, PTR_SIZE, &got) && got == PTR_SIZE && *out;
}

static ULONG_PTR process_params(HANDLE h)
{
    PROCESS_BASIC_INFORMATION pbi;
    ULONG rl;
    ULONG_PTR params;
    if (NtQueryInformationProcess(h, ProcessBasicInformation, &pbi, sizeof pbi, &rl)) return 0;
    if (!read_ptr(h, (ULONG_PTR)pbi.PebBaseAddress + PEB_PARAMS, &params)) return 0;
    return params;
}

static int env_has(HANDLE h, const wchar_t *marker)
{
    ULONG_PTR params = process_params(h), env;
    static wchar_t buf[1 << 17];
    SIZE_T n = 0, got;
    size_t wn, len = wcslen(marker), i;
    if (!params || !read_ptr(h, params + PARAMS_ENV, &env)) return 0;
    while (n < sizeof buf - 4096 && ReadProcessMemory(h, (char *)env + n, (char *)buf + n, 4096, &got) && got) n += got;
    wn = n / sizeof(wchar_t);
    for (i = 0; i + len <= wn;) {
        if (!wcsncmp(buf + i, marker, len) && (i + len == wn || buf[i + len] == 0)) return 1;
        while (i < wn && buf[i]) i++;
        if (++i < wn && !buf[i]) break;
    }
    return 0;
}

static void print_cmdline(HANDLE h)
{
    ULONG_PTR params = process_params(h), bufp;
    USHORT len = 0;
    static wchar_t cl[4096];
    SIZE_T got;
    if (!params) return;
    if (!ReadProcessMemory(h, (char *)params + PARAMS_CMDLINE, &len, sizeof len, &got) || got != sizeof len) return;
    if (!read_ptr(h, params + PARAMS_CMDLINE + (PTR_SIZE == 8 ? 8 : 4), &bufp)) return;
    if (len >= sizeof cl - sizeof(wchar_t)) len = sizeof cl - 2 * sizeof(wchar_t);
    if (!ReadProcessMemory(h, (void *)bufp, cl, len, &got)) return;
    cl[got / sizeof(wchar_t)] = 0;
    printf("command line: %ls\n", cl);
}

int main(int argc, char **argv)
{
    wchar_t want[260], marker[512];
    HANDLE snap, hp;
    PROCESSENTRY32W pe = { sizeof pe };
    THREADENTRY32 te = { sizeof te };
    DWORD pid = 0;
    int matches = 0;
    BOOL wow = FALSE, ok;

    if (argc < 3) return 2;
    MultiByteToWideChar(CP_ACP, 0, argv[2], -1, want, 260);
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, marker, 512);
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD, 0);
    for (ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        HANDLE h;
        if (_wcsicmp(pe.szExeFile, want)) continue;
        h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
        if (!h) continue;
        if (env_has(h, marker)) {
            printf("match pid %08lx %ls\n", pe.th32ProcessID, pe.szExeFile);
            pid = pe.th32ProcessID;
            matches++;
        }
        CloseHandle(h);
    }
    if (matches != 1) {
        printf("matches=%d\n", matches);
        return 1;
    }
    hp = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hp) {
        printf("cannot open pid %08lx: %lu\n", pid, GetLastError());
        return 1;
    }
    print_cmdline(hp);
    IsWow64Process(hp, &wow);
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    if (!SymInitialize(hp, NULL, FALSE)) printf("SymInitialize failed %lu\n", GetLastError());
    {
        HMODULE mods[512];
        DWORD need = 0, i;
        if (EnumProcessModulesEx(hp, mods, sizeof mods, &need, wow ? LIST_MODULES_32BIT : LIST_MODULES_ALL)) {
            for (i = 0; i < need / sizeof(HMODULE) && i < 512; i++) {
                wchar_t path[MAX_PATH];
                MODULEINFO mi;
                if (!GetModuleFileNameExW(hp, mods[i], path, MAX_PATH) || !GetModuleInformation(hp, mods[i], &mi, sizeof mi)) continue;
                if (!SymLoadModuleExW(hp, NULL, path, NULL, (DWORD64)(ULONG_PTR)mi.lpBaseOfDll, mi.SizeOfImage, NULL, 0))
                    printf("load %ls failed %lu\n", path, GetLastError());
            }
        } else
            printf("EnumProcessModulesEx failed %lu\n", GetLastError());
    }
    for (ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        HANDLE ht;
        STACKFRAME64 f;
        DWORD mach;
        void *ctxp;
        CONTEXT c;
        WOW64_CONTEXT wc;
        int i;
        if (te.th32OwnerProcessID != pid) continue;
        ht = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!ht) continue;
        memset(&f, 0, sizeof f);
        memset(&c, 0, sizeof c);
        memset(&wc, 0, sizeof wc);
        /* Suspended only while its registers are read: a blocked thread's stack does not move while
           it is walked, and a kill of this helper must never leave a thread stopped. */
        SuspendThread(ht);
#ifdef _WIN64
        if (wow) {
            wc.ContextFlags = WOW64_CONTEXT_FULL;
            Wow64GetThreadContext(ht, &wc);
            f.AddrPC.Offset = wc.Eip; f.AddrFrame.Offset = wc.Ebp; f.AddrStack.Offset = wc.Esp;
            mach = IMAGE_FILE_MACHINE_I386; ctxp = &wc;
        } else {
            c.ContextFlags = CONTEXT_FULL;
            GetThreadContext(ht, &c);
            f.AddrPC.Offset = c.Rip; f.AddrFrame.Offset = c.Rbp; f.AddrStack.Offset = c.Rsp;
            mach = IMAGE_FILE_MACHINE_AMD64; ctxp = &c;
        }
#else
        c.ContextFlags = CONTEXT_FULL;
        GetThreadContext(ht, &c);
        f.AddrPC.Offset = c.Eip; f.AddrFrame.Offset = c.Ebp; f.AddrStack.Offset = c.Esp;
        mach = IMAGE_FILE_MACHINE_I386; ctxp = &c;
#endif
        ResumeThread(ht);
        f.AddrPC.Mode = f.AddrFrame.Mode = f.AddrStack.Mode = AddrModeFlat;
        printf("Backtracing for thread %04lx in process %04lx:\nBacktrace:\n", te.th32ThreadID, pid);
        for (i = 0; i < 40 && StackWalk64(mach, hp, ht, &f, ctxp, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL) && f.AddrPC.Offset; i++) {
            char sb[sizeof(SYMBOL_INFO) + 256];
            SYMBOL_INFO *s = (SYMBOL_INFO *)sb;
            DWORD64 disp = 0;
            IMAGEHLP_MODULE64 m = { sizeof m };
            const char *mod = SymGetModuleInfo64(hp, f.AddrPC.Offset, &m) ? m.ModuleName : "?";
            memset(sb, 0, sizeof sb);
            s->SizeOfStruct = sizeof(SYMBOL_INFO);
            s->MaxNameLen = 255;
            if (SymFromAddr(hp, f.AddrPC.Offset, &disp, s))
                printf("  %d 0x%016llx %s+0x%llx in %s\n", i, f.AddrPC.Offset, s->Name, disp, mod);
            else
                printf("  %d 0x%016llx in %s\n", i, f.AddrPC.Offset, mod);
        }
        CloseHandle(ht);
    }
    SymCleanup(hp);
    return 0;
}
