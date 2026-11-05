// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport Windows: the POSIX calls of the compatibility headers in include/ (sys/mman.h, dlfcn.h,
// sys/resource.h, sys/uio.h, execinfo.h) over the Win32 API. Only what the GPU library uses.
#include <windows.h>
#include "shim/bb_env.h"
#include <timeapi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <psapi.h>
#include <tlhelp32.h>

#include <dlfcn.h>
#include <execinfo.h>
#include <io.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <sys/ucontext.h>

#include <atomic>

namespace {

thread_local std::string dl_error;

std::wstring Wide(const char* utf8) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring out(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out.data(), n);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

void SetDlError(const char* what) {
    dl_error = std::string(what) + " (Windows error " + std::to_string(GetLastError()) + ")";
}

// sigaction (bbport_signal.h): the handlers, and the one vectored exception handler that runs them.
std::atomic<bool> handler_added{false};
struct sigaction actions[BB_NSIG];
SRWLOCK actions_lock = SRWLOCK_INIT;

LONG CALLBACK DispatchException(EXCEPTION_POINTERS* pointers) {
    const EXCEPTION_RECORD* record = pointers->ExceptionRecord;
    int sig = 0;
    siginfo_t info{};
    unsigned long long error = 0;
    unsigned long long rip_adjust = 0;
    switch (record->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
        sig = SIGSEGV;
        info.si_addr = reinterpret_cast<void*>(record->ExceptionInformation[1]);
        info.si_code = SEGV_ACCERR;
        // Page fault error code bits as Linux reports them: 2 a write, 16 an instruction fetch.
        error = record->ExceptionInformation[0] == 1 ? 2 : record->ExceptionInformation[0] == 8 ? 16 : 0;
        break;
    case EXCEPTION_BREAKPOINT:
        sig = SIGTRAP;
        rip_adjust = 1; // Linux reports the instruction after the int3
        info.si_addr = record->ExceptionAddress;
        break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
        sig = SIGILL;
        info.si_addr = record->ExceptionAddress;
        break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
        sig = SIGFPE;
        info.si_addr = record->ExceptionAddress;
        break;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
    struct sigaction action;
    AcquireSRWLockShared(&actions_lock);
    action = actions[sig];
    ReleaseSRWLockShared(&actions_lock);
    if (action.sa_handler == SIG_DFL || action.sa_handler == SIG_IGN) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    info.si_signo = sig;
    ucontext_t context{};
    CONTEXT* c = pointers->ContextRecord;
    BbWin32ToUcontext(c, error, &context);
    context.uc_mcontext.gregs[REG_RIP] += greg_t(rip_adjust);
    context.uc_mcontext.gregs[REG_TRAPNO] = sig == SIGSEGV ? 14 : sig == SIGTRAP ? 3 : 0;
    context.uc_mcontext.gregs[REG_CR2] = greg_t(reinterpret_cast<unsigned long long>(info.si_addr));
    context.uc_mcontext.fpregs = &c->FltSave;
    if (action.sa_flags & SA_SIGINFO) {
        action.sa_sigaction(sig, &info, &context);
    } else {
        action.sa_handler(sig);
    }
    BbUcontextToWin32(&context, c);
    return EXCEPTION_CONTINUE_EXECUTION;
}

} // namespace

extern "C" {

int sigaction(int sig, const struct sigaction* action, struct sigaction* previous) {
    if (sig <= 0 || sig >= BB_NSIG) {
        errno = EINVAL;
        return -1;
    }
    AcquireSRWLockExclusive(&actions_lock);
    if (previous) {
        *previous = actions[sig];
    }
    if (action) {
        actions[sig] = *action;
    }
    ReleaseSRWLockExclusive(&actions_lock);
    if (action && !handler_added.exchange(true)) {
        AddVectoredExceptionHandler(1, DispatchException);
    }
    return 0;
}

int pause(void) {
    Sleep(INFINITE);
    return -1;
}


int madvise(void* address, size_t size, int advice) {
    if (advice == MADV_DONTNEED) {
        // The pages read as zero again: decommitted and committed afresh.
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(address, &info, sizeof(info)) && info.Type == MEM_PRIVATE) {
            VirtualAlloc(address, size, MEM_RESET, PAGE_NOACCESS);
        }
    }
    return 0;
}

void* dlopen(const char* path, int flags) {
    HMODULE module = nullptr;
    if (!path) {
        module = GetModuleHandleW(nullptr);
    } else if (flags & RTLD_NOLOAD) {
        module = GetModuleHandleW(Wide(path).c_str());
    } else {
        module = LoadLibraryExW(Wide(path).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    if (!module) SetDlError(path ? path : "(self)");
    return module;
}

void* dlsym(void* module, const char* name) {
    void* p = reinterpret_cast<void*>(
        GetProcAddress(module ? static_cast<HMODULE>(module) : GetModuleHandleW(nullptr), name));
    if (!p) SetDlError(name);
    return p;
}

int dlclose(void* module) {
    return module && FreeLibrary(static_cast<HMODULE>(module)) ? 0 : -1;
}

const char* dlerror(void) {
    if (dl_error.empty()) return nullptr;
    thread_local std::string last;
    last = std::move(dl_error);
    dl_error.clear();
    return last.c_str();
}

int dladdr(const void* address, Dl_info* info) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module)) {
        return 0;
    }
    thread_local char path[MAX_PATH * 3];
    wchar_t wide[MAX_PATH];
    const DWORD n = GetModuleFileNameW(module, wide, MAX_PATH);
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, int(n), path, sizeof(path) - 1, nullptr, nullptr);
    path[bytes > 0 ? bytes : 0] = 0;
    info->dli_fname = path;
    info->dli_fbase = module;
    info->dli_sname = nullptr;
    info->dli_saddr = nullptr;
    return 1;
}

int getrusage(int who, struct rusage* usage) {
    FILETIME created, exited, kernel, user;
    const BOOL ok = who == RUSAGE_THREAD
                        ? GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)
                        : GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    if (!ok) return -1;
    const auto us = [](FILETIME t) {
        return ((static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime) / 10;
    };
    std::memset(usage, 0, sizeof(*usage));
    usage->ru_utime.tv_sec = long(us(user) / 1000000);
    usage->ru_utime.tv_usec = long(us(user) % 1000000);
    usage->ru_stime.tv_sec = long(us(kernel) / 1000000);
    usage->ru_stime.tv_usec = long(us(kernel) % 1000000);
    return 0;
}

ssize_t process_vm_readv(int pid, const struct iovec* local, unsigned long local_count,
                         const struct iovec* remote, unsigned long remote_count,
                         unsigned long flags) {
    (void)pid;
    (void)flags;
    if (local_count != 1 || remote_count != 1) {
        errno = EINVAL;
        return -1;
    }
    SIZE_T done = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), remote->iov_base, local->iov_base,
                           std::min(local->iov_len, remote->iov_len), &done)) {
        errno = EFAULT;
        return done ? ssize_t(done) : -1;
    }
    return ssize_t(done);
}

long syscall(long number, ...) {
    if (number == 186) { // SYS_gettid
        return long(GetCurrentThreadId());
    }
    errno = ENOSYS;
    return -1;
}

int bb_guest_tls_slot(void) {
    // A slot of the TEB's inline array (64 of them), the same for every thread.
    static const int slot = [] {
        const DWORD index = TlsAlloc();
        if (index == TLS_OUT_OF_INDEXES || index >= 64) {
            std::fprintf(stderr, "STOP: no TEB TLS slot below 64 for the guest's thread pointer\n");
            std::exit(21);
        }
        return int(index);
    }();
    return slot;
}

int bb_clock_nanosleep(clockid_t clock, int flags, const struct timespec* request,
                       struct timespec* remain) {
    long long ns = (long long)request->tv_sec * 1000000000ll + request->tv_nsec;
    if (flags & TIMER_ABSTIME) {
        struct timespec now{};
        if (clock_gettime(clock, &now) != 0) {
            return EINVAL;
        }
        ns -= (long long)now.tv_sec * 1000000000ll + now.tv_nsec;
    }
    if (remain) {
        *remain = {};
    }
    if (ns <= 0) {
        return 0;
    }
    // A timer per thread: high resolution (Windows 10 1803+), else the 1 ms period (timeBeginPeriod).
    static thread_local HANDLE timer = [] {
        HANDLE t = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_ALL_ACCESS);
        return t ? t : CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }();
    LARGE_INTEGER due{};
    due.QuadPart = -std::max(1ll, ns / 100);
    if (!timer || !SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
        Sleep(DWORD((ns + 999999) / 1000000));
        return 0;
    }
    WaitForSingleObject(timer, INFINITE);
    return 0;
}


namespace {
// BB_SAMPLE_THREAD=<name> (diagnostics, Windows): where that thread runs, sampled every 0.5 ms by
// suspending it; every 10 s the top addresses as exe+offset (llvm-symbolizer on the unstripped
// bb-probe.exe), with the share of samples outside the exe (system DLLs: drivers, ntdll).
void SampleThread(HANDLE thread, std::string name) {
    std::unordered_map<DWORD64, unsigned> counts;
    std::unordered_map<DWORD64, unsigned> callers;
    unsigned total = 0;
    const auto base = reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr));
    MODULEINFO info{};
    K32GetModuleInformation(GetCurrentProcess(), GetModuleHandleW(nullptr), &info, sizeof(info));
    const DWORD64 end = base + info.SizeOfImage;
    // Return addresses only in the code section (data holds function pointers that look alike).
    DWORD64 text_lo = base, text_hi = end;
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if (std::memcmp(section->Name, ".text", 5) == 0) {
                text_lo = base + section->VirtualAddress;
                text_hi = text_lo + section->Misc.VirtualSize;
            }
        }
    }
    DWORD last = GetTickCount();
    for (;;) {
        const timespec pause{0, 500000};
        bb_clock_nanosleep(CLOCK_MONOTONIC, 0, &pause, nullptr);
        if (SuspendThread(thread) == DWORD(-1)) {
            return;
        }
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(thread, &context)) {
            if (context.Rip >= base && context.Rip < end) {
                ++counts[context.Rip - base];
            } else {
                // Outside the exe: the module, and the exe code that called it (the first return
                // address into the exe on the stack), as a key with bit 62 set.
                ++counts[context.Rip];
                const auto* stack = reinterpret_cast<const DWORD64*>(context.Rsp);
                for (int i = 0; i < 512; ++i) {
                    const DWORD64 value = stack[i];
                    if (value >= text_lo && value < text_hi) {
                        ++callers[value - base];
                        break;
                    }
                }
            }
            ++total;
        }
        ResumeThread(thread);
        if (GetTickCount() - last >= 10000 && total) {
            last = GetTickCount();
            std::vector<std::pair<unsigned, DWORD64>> top;
            for (const auto& [rip, n] : counts) {
                top.emplace_back(n, rip);
            }
            std::sort(top.rbegin(), top.rend());
            std::printf("Thread samples %s (%u):", name.c_str(), total);
            std::unordered_map<std::string, unsigned> modules; // outside the exe, by module
            for (const auto& [n, rip] : top) {
                if (rip < end - base) continue;
                HMODULE module = nullptr;
                char path[MAX_PATH] = "?";
                if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       reinterpret_cast<LPCSTR>(rip), &module)) {
                    GetModuleFileNameA(module, path, MAX_PATH);
                }
                const char* slash = std::strrchr(path, '\\');
                modules[slash ? slash + 1 : path] += n;
            }
            for (const auto& [module, n] : modules) {
                std::printf(" [%s %.1f%%]", module.c_str(), 100.0 * n / total);
            }
            for (size_t i = 0, shown = 0; i < top.size() && shown < 40; ++i) {
                if (top[i].second >= end - base) continue;
                ++shown;
                std::printf(" exe+%#llx %.1f%%", (unsigned long long)top[i].second, 100.0 * top[i].first / total);
            }
            std::vector<std::pair<unsigned, DWORD64>> top_callers;
            for (const auto& [rip, n] : callers) {
                top_callers.emplace_back(n, rip);
            }
            std::sort(top_callers.rbegin(), top_callers.rend());
            std::printf(" | callers outside:");
            for (size_t i = 0; i < top_callers.size() && i < 25; ++i) {
                std::printf(" exe+%#llx %.1f%%", (unsigned long long)top_callers[i].second,
                            100.0 * top_callers[i].first / total);
            }
            callers.clear();
            std::printf("\n");
            std::fflush(stdout);
            counts.clear();
            total = 0;
        }
    }
}
} // namespace
void bb_win32_set_thread_name(const char* name) {
    wchar_t wide[64];
    const int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 64);
    if (n > 0) {
        wide[63] = 0;
        SetThreadDescription(GetCurrentThread(), wide);
    }
    static const char* sampled = bb_env("BB_SAMPLE_THREAD");
    if (sampled && *sampled && std::strcmp(sampled, name) == 0) {
        HANDLE self = nullptr;
        if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self,
                            THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                            FALSE, 0)) {
            std::printf("Runtime: sampling thread %s\n", name);
            std::thread{SampleThread, self, std::string{name}}.detach();
        }
    }
}

void bb_win32_describe_address(const void* address, char* text, unsigned size) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(address, &mbi, sizeof(mbi))) {
        std::snprintf(text, size, "VirtualQuery failed (%lu)", GetLastError());
        return;
    }
    std::snprintf(text, size, "region %p+%#llx state %#lx protect %#lx type %#lx, allocation %p protect %#lx",
                  mbi.BaseAddress, (unsigned long long)mbi.RegionSize, mbi.State, mbi.Protect, mbi.Type,
                  mbi.AllocationBase, mbi.AllocationProtect);
}

// CommandProcessor::ReportStall: the Linux report has the runtime's SIGUSR2 handler walk every
// thread's frame-pointer chain (/proc/self/task); Windows has neither, so a stalled guest left the
// log with no thread at all -- the report could say the GPU was idle and nothing about where the
// game stopped. This reads each thread's registers while it is suspended (CONTEXT_FULL) and walks
// the stack with RtlVirtualUnwind over the x64 unwind tables (.pdata), which is how Windows itself
// walks one.
//
// [extra] It first used the frame-pointer chain only. The first real report showed why that is not
// enough: every thread parked in a system call comes back with rbp = 0 (the transition into the
// kernel does not keep it), so the walk had no frame to start from and printed no chain at all --
// 121 threads, none with a frame (2026-11-05). RtlVirtualUnwind does not need rbp; it reads the
// unwind data of each module, so it crosses ntdll/kernel32 and reaches the port's frames.
void bb_win32_dump_threads(uintptr_t guest_lo, uintptr_t guest_hi) {
    using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    const auto describe = reinterpret_cast<GetThreadDescriptionFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                               "GetThreadDescription")));
    const HANDLE self = GetCurrentProcess();
    const DWORD self_id = GetCurrentThreadId();
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    MODULEINFO info{};
    if (!K32GetModuleInformation(self, GetModuleHandleW(nullptr), &info, sizeof(info))) {
        std::printf("  thread dump: no module information (%lu)\n", GetLastError());
        std::fflush(stdout);
        return;
    }
    const auto end = base + uintptr_t(info.SizeOfImage);
    // The exe's code: used to tell port frames from the rest when the chain is printed.
    uintptr_t text_lo = base, text_hi = end;
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if (std::memcmp(section->Name, ".text", 5) == 0) {
                text_lo = base + section->VirtualAddress;
                text_hi = text_lo + section->Misc.VirtualSize;
            }
        }
    }
    // A stack read from another thread: only committed pages without the guard (crossing one would
    // fault this thread), and never across the end of the region.
    const auto readable = [](uintptr_t address, uintptr_t bytes) {
        MEMORY_BASIC_INFORMATION mbi{};
        return VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)) &&
               mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0 &&
               address + bytes <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    };
    // How far the committed region around `address` reaches above it, 0 if it is not readable.
    // RtlVirtualUnwind reads the stack on its own, so this is what says a stack pointer can be
    // handed to it without risking a fault in the thread doing the dump.
    const auto room = [](uintptr_t address) -> uintptr_t {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)) ||
            mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return 0;
        }
        const auto end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        return address < end ? end - address : 0;
    };
    // [`guest_lo`, `guest_hi`) and the exe: both printed image-relative, as the other logs quote
    // them (the guest's eboot at its image base, the port's code at the exe's). Anything else names
    // its module (`ntdll.dll+0x...`): the first report showed a wall of bare hex in a system DLL,
    // where the only way to read it was to know the addresses by heart.
    const auto where = [&](uintptr_t address, char* text, unsigned size) {
        if (guest_lo && address >= guest_lo && address < guest_hi) {
            std::snprintf(text, size, "game+%#llx", (unsigned long long)(address - guest_lo));
            return;
        }
        if (address >= base && address < end) {
            std::snprintf(text, size, "exe+%#llx", (unsigned long long)(address - base));
            return;
        }
        HMODULE module = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(address), &module)) {
            char path[MAX_PATH] = "?";
            GetModuleFileNameA(module, path, MAX_PATH);
            const char* slash = std::strrchr(path, '\\');
            std::snprintf(text, size, "%s+%#llx", slash ? slash + 1 : path,
                          (unsigned long long)(address - reinterpret_cast<uintptr_t>(module)));
            return;
        }
        std::snprintf(text, size, "%#llx", (unsigned long long)address);
    };
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        std::printf("  thread dump: no snapshot (%lu)\n", GetLastError());
        std::fflush(stdout);
        return;
    }
    // One line per thread, built while it is suspended and printed after it is running again: a
    // suspended thread may hold the stdout lock, so nothing here allocates or writes until every
    // thread let go. Fixed sizes for the same reason (no string growth under a held thread).
    constexpr unsigned kLines = 256, kLineSize = 512;
    static char lines[kLines][kLineSize];
    unsigned used = 0;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Thread32First(snapshot, &entry); ok && used < kLines;
         ok = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == self_id) {
            continue; // this thread is the one printing
        }
        const HANDLE thread =
            OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION,
                       FALSE, entry.th32ThreadID);
        if (!thread) {
            continue;
        }
        char name[128] = "";
        PWSTR wide = nullptr;
        if (describe && SUCCEEDED(describe(thread, &wide)) && wide) {
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, name, sizeof(name), nullptr, nullptr);
            LocalFree(wide);
        }
        if (SuspendThread(thread) == DWORD(-1)) {
            CloseHandle(thread);
            continue;
        }
        // CONTEXT_FULL, not CONTEXT_CONTROL: RtlVirtualUnwind writes the whole register set of the
        // caller as it unwinds, and the integer half has to be there to read.
        CONTEXT context{};
        context.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(thread, &context)) {
            char* line = lines[used];
            char rip[64], rsp[64];
            where(uintptr_t(context.Rip), rip, sizeof(rip));
            where(uintptr_t(context.Rsp), rsp, sizeof(rsp));
            int at = std::snprintf(line, kLineSize, "  thread %5lu %-28s rip %s rsp %s",
                                   entry.th32ThreadID, name[0] ? name : "?", rip, rsp);
            at = std::min(at > 0 ? at : 0, int(kLineSize) - 1);
            // The chain, one frame at a time: RtlVirtualUnwind for anything with x64 unwind data
            // (the port and every system DLL), the frame pointer for the game's image, which runs
            // natively but has no .pdata registered here (and does keep rbp: its functions start
            // with `push rbp; mov rbp, rsp`). Only the frames that are port code or game code are
            // printed: a chain of eight ntdll/kernel32 frames says nothing, and the game's frames
            // are the ones that say where the guest is.
            for (unsigned depth = 0; depth < 64 && at > 0 && unsigned(at) < kLineSize - 40; ++depth) {
                const auto pc = uintptr_t(context.Rip);
                if (pc == 0) {
                    break;
                }
                const bool in_exe = pc >= text_lo && pc < text_hi;
                const bool in_game = guest_lo && pc >= guest_lo && pc < guest_hi;
                if (in_exe || in_game) {
                    char shown[32];
                    where(pc, shown, sizeof(shown));
                    // snprintf returns the length it would have written; clamped, so a full line
                    // cannot make the next write walk off the fixed buffer.
                    const int add = std::snprintf(line + at, kLineSize - unsigned(at), " <- %s", shown);
                    if (add > 0) {
                        at = std::min(at + add, int(kLineSize) - 1);
                    }
                }
                const auto rsp_before = uintptr_t(context.Rsp);
                // RtlVirtualUnwind reads the stack itself, and a chain that stopped being one
                // (a reused stack, a frame pointer that was data) can point it anywhere. Hand it
                // only what is committed without the guard: the top of the frame plus the window
                // above it that one frame's unwind reads. At the very end of a region the rest is
                // refused instead of faulting in the thread doing the dump.
                if (room(rsp_before) < 128) {
                    break;
                }
                // A return address is the instruction after the call, and that can already be the
                // next function (or outside the module). Look the entry up just before it; the top
                // frame's PC is exact.
                const auto lookup_pc = DWORD64(depth == 0 ? pc : pc - 1);
                DWORD64 image_base = 0;
                PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(lookup_pc, &image_base, nullptr);
                if (function) {
                    PVOID handler_data = nullptr;
                    DWORD64 establisher = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, lookup_pc, function, &context,
                                     &handler_data, &establisher, nullptr);
                } else if (in_game) {
                    // No unwind data for the game's image: its own frame pointer is the only way
                    // on. Accepted only while it is a sound frame (aligned, committed, above the
                    // stack pointer, strictly climbing) and its return address lands in code
                    // (either image), which is what keeps a data word from passing for a frame.
                    const auto frame = uintptr_t(context.Rbp);
                    if ((frame & 7) != 0 || frame <= rsp_before || !readable(frame, 2 * sizeof(uintptr_t))) {
                        break;
                    }
                    const auto* words = reinterpret_cast<const uintptr_t*>(frame);
                    const uintptr_t caller = words[1], next = words[0];
                    const bool caller_in_exe = caller >= text_lo && caller < text_hi;
                    const bool caller_in_game = guest_lo && caller >= guest_lo && caller < guest_hi;
                    if ((!caller_in_exe && !caller_in_game) || next <= frame) {
                        break;
                    }
                    context.Rip = DWORD64(caller);
                    context.Rsp = DWORD64(frame + 2 * sizeof(uintptr_t));
                    context.Rbp = DWORD64(next);
                } else {
                    // A leaf (or a PC with no entry): its return address sits at the stack pointer.
                    // It must at least land in some loaded module (or the game's image): a data
                    // word that happens to be in range would otherwise pass for a frame.
                    if (!readable(rsp_before, sizeof(uintptr_t))) {
                        break;
                    }
                    const auto caller = *reinterpret_cast<const uintptr_t*>(rsp_before);
                    HMODULE owner = nullptr;
                    const bool caller_in_module =
                        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           reinterpret_cast<LPCSTR>(caller), &owner) != 0;
                    const bool caller_in_game =
                        guest_lo && caller >= guest_lo && caller < guest_hi;
                    if (!caller_in_module && !caller_in_game) {
                        break;
                    }
                    context.Rip = DWORD64(caller);
                    context.Rsp = DWORD64(rsp_before + sizeof(uintptr_t));
                }
                if (uintptr_t(context.Rsp) <= rsp_before && uintptr_t(context.Rip) != 0) {
                    break; // no progress: the chain is not one (a stale or reused stack)
                }
            }
            if (at > 0) {
                ++used;
            }
        }
        ResumeThread(thread);
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    for (unsigned i = 0; i < used; ++i) {
        std::printf("%s\n", lines[i]);
    }
    std::printf("  thread dump: %u threads (where each is, then the port's and the game's frames)\n", used);
    std::fflush(stdout);
}

void bb_guest_tls_set(void* tcb) {
    TlsSetValue(DWORD(bb_guest_tls_slot()), tcb);
}

} // extern "C"

// The slot is taken while the process starts: the GPU driver and the libraries take TLS indices
// as they load (NVIDIA's took the last of the 64 inline ones on a GTX 1050 laptop, Windows 10,
// before the guest asked for its slot).
namespace {
[[maybe_unused]] const int early_guest_tls_slot = bb_guest_tls_slot();
// Sleeps (libc++'s sleep_for, condition variable timeouts) in 1 ms steps, not 15.6 ms.
[[maybe_unused]] const bool timer_period = timeBeginPeriod(1) == TIMERR_NOERROR;

// The game first, the system not starved: the process a class above normal (the CPU goes to it
// before the system's and other programs' normal-priority work; audio, input and the compositor
// run higher anyway), no power throttling (EcoQoS: lower clocks, efficiency cores), and its GPU
// work above the other processes'. The threads keep their order among themselves: raising some
// alone (BB_STAGE_PRIORITY) had the game hand over half-written command buffers at loads.
// BB_PROCESS_PRIORITY=normal|above|high (default above).
bool RaiseProcessPriority() {
    const char* env = bb_env("BB_PROCESS_PRIORITY");
    const std::string level = env && *env ? env : "above";
    if (level == "normal" || level == "0") {
        return false;
    }
    const bool high = level == "high";
    const HANDLE process = GetCurrentProcess();
    const bool cpu =
        SetPriorityClass(process, high ? HIGH_PRIORITY_CLASS : ABOVE_NORMAL_PRIORITY_CLASS);
    PROCESS_POWER_THROTTLING_STATE throttling{
        .Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION,
        .ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
                       PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION,
        .StateMask = 0,
    };
    bool power =
        SetProcessInformation(process, ProcessPowerThrottling, &throttling, sizeof(throttling));
    if (!power) {
        // Windows 10 refuses the timer resolution flag (Windows 11).
        throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        power =
            SetProcessInformation(process, ProcessPowerThrottling, &throttling, sizeof(throttling));
    }
    // gdi32's D3DKMTSetProcessSchedulingPriorityClass: 3 above normal, 4 high.
    using SetGpuPriority = LONG(WINAPI*)(HANDLE, int);
    bool gpu = false;
    if (const HMODULE gdi = LoadLibraryA("gdi32.dll")) {
        const auto proc = GetProcAddress(gdi, "D3DKMTSetProcessSchedulingPriorityClass");
        if (const auto set = reinterpret_cast<SetGpuPriority>(reinterpret_cast<void*>(proc))) {
            gpu = set(process, high ? 4 : 3) == 0;
        }
    }
    std::fprintf(stderr, "Process priority: %s (CPU %s, power throttling %s, GPU %s)\n",
                 high ? "high" : "above normal", cpu ? "set" : "refused", power ? "off" : "kept",
                 gpu ? "set" : "refused");
    return cpu;
}
[[maybe_unused]] const bool process_priority = RaiseProcessPriority();
}

extern "C" {

int gettid(void) {
    return int(GetCurrentThreadId());
}

long sysconf(int name) {
    switch (name) {
    case _SC_PAGESIZE:
        return 4096;
    case _SC_NPROCESSORS_ONLN:
        return long(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    default:
        errno = EINVAL;
        return -1;
    }
}

unsigned bb_win32_available_cpus(void) {
    DWORD_PTR process = 0, system = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process, &system) && process) {
        return unsigned(__builtin_popcountll(process));
    }
    return std::max(1u, unsigned(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)));
}

void bb_win32_background_thread(void) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
}

void bb_win32_critical_thread(int level) {
    SetThreadPriority(GetCurrentThread(),
                      level >= 2 ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_ABOVE_NORMAL);
}

int backtrace(void** buffer, int size) {
    return RtlCaptureStackBackTrace(1, DWORD(size), buffer, nullptr);
}

void backtrace_symbols_fd(void* const* buffer, int size, int fd) {
    for (int i = 0; i < size; ++i) {
        Dl_info info{};
        char line[600];
        const int n = dladdr(buffer[i], &info)
                          ? std::snprintf(line, sizeof(line), "%s+%#llx\n", info.dli_fname,
                                          (unsigned long long)(static_cast<const char*>(buffer[i]) -
                                                               static_cast<const char*>(info.dli_fbase)))
                          : std::snprintf(line, sizeof(line), "%p\n", buffer[i]);
        _write(fd, line, unsigned(n));
    }
}

} // extern "C"

// bb_setjmp/bb_longjmp (bbport_jmp.h): the Win64 callee-saved state, no SEH unwinding (guest
// frames between them have no unwind data). Slots: rbx rbp rdi rsi r12-r15, the caller's rsp
// and rip, MXCSR and the x87 control word, xmm6-15.
// bb_run_on_stack(function, argument, low, high): calls function(argument) on the stack
// [low, high), the TEB's stack bounds set to it meanwhile (Windows checks them).
asm(R"(
    .text
    .globl bb_setjmp
    .def bb_setjmp; .scl 2; .type 32; .endef
bb_setjmp:
    mov %rbx, 0(%rcx)
    mov %rbp, 8(%rcx)
    mov %rdi, 16(%rcx)
    mov %rsi, 24(%rcx)
    mov %r12, 32(%rcx)
    mov %r13, 40(%rcx)
    mov %r14, 48(%rcx)
    mov %r15, 56(%rcx)
    lea 8(%rsp), %rdx
    mov %rdx, 64(%rcx)
    mov (%rsp), %rdx
    mov %rdx, 72(%rcx)
    stmxcsr 80(%rcx)
    fnstcw 84(%rcx)
    movdqu %xmm6, 96(%rcx)
    movdqu %xmm7, 112(%rcx)
    movdqu %xmm8, 128(%rcx)
    movdqu %xmm9, 144(%rcx)
    movdqu %xmm10, 160(%rcx)
    movdqu %xmm11, 176(%rcx)
    movdqu %xmm12, 192(%rcx)
    movdqu %xmm13, 208(%rcx)
    movdqu %xmm14, 224(%rcx)
    movdqu %xmm15, 240(%rcx)
    xor %eax, %eax
    ret

    .globl bb_longjmp
    .def bb_longjmp; .scl 2; .type 32; .endef
bb_longjmp:
    mov %edx, %eax
    test %eax, %eax
    jnz 1f
    inc %eax
1:
    mov 0(%rcx), %rbx
    mov 8(%rcx), %rbp
    mov 16(%rcx), %rdi
    mov 24(%rcx), %rsi
    mov 32(%rcx), %r12
    mov 40(%rcx), %r13
    mov 48(%rcx), %r14
    mov 56(%rcx), %r15
    ldmxcsr 80(%rcx)
    fnclex
    fldcw 84(%rcx)
    movdqu 96(%rcx), %xmm6
    movdqu 112(%rcx), %xmm7
    movdqu 128(%rcx), %xmm8
    movdqu 144(%rcx), %xmm9
    movdqu 160(%rcx), %xmm10
    movdqu 176(%rcx), %xmm11
    movdqu 192(%rcx), %xmm12
    movdqu 208(%rcx), %xmm13
    movdqu 224(%rcx), %xmm14
    movdqu 240(%rcx), %xmm15
    mov 64(%rcx), %rsp
    jmp *72(%rcx)

    .globl bb_run_on_stack
    .def bb_run_on_stack; .scl 2; .type 32; .endef
bb_run_on_stack:
    push %rbp
    mov %rsp, %rbp
    push %rbx
    push %rsi
    push %rdi
    push %r12
    mov %gs:8, %r12
    push %r12
    mov %gs:16, %r12
    push %r12
    mov %r9, %gs:8
    mov %r8, %gs:16
    mov %rcx, %rax
    mov %rdx, %rcx
    mov %r9, %rsp
    and $-16, %rsp
    sub $32, %rsp
    call *%rax
    lea -48(%rbp), %rsp
    pop %r12
    mov %r12, %gs:16
    pop %r12
    mov %r12, %gs:8
    pop %r12
    pop %rdi
    pop %rsi
    pop %rbx
    pop %rbp
    ret
)");
