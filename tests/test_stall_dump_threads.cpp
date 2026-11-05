// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bb_win32_dump_threads: the Windows equivalent of the SIGUSR2 stack dump in the stall report.
//
// [extra] This exists because the dump is the only way to find out where the game stopped when it
// stops submitting (CommandProcessor::ReportStall prints the GPU is idle, and that is all it can
// say). It runs nowhere else: only on a stall, once per run. So it went unnoticed that it printed
// 121 threads with no stack at all -- every thread was parked in a system call with rbp = 0, and
// the walk only knew frame pointers (2026-11-05). This test exercises the real function.
#include <windows.h>
#include <io.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

void bb_win32_dump_threads(uintptr_t guest_lo, uintptr_t guest_hi);

namespace {

/// Reads a whole file into a string (the dump is written to a file this test redirects stdout to).
std::string Slurp(const char* path) {
    std::string text;
    FILE* file = std::fopen(path, "rb");
    if (!file) {
        return text;
    }
    char chunk[4096];
    for (size_t got; (got = std::fread(chunk, 1, sizeof(chunk), file)) > 0;) {
        text.append(chunk, got);
    }
    std::fclose(file);
    return text;
}

} // namespace

int main() {
    char path[MAX_PATH] = "";
    GetTempPathA(MAX_PATH, path);
    std::string file = std::string(path) + "bbport_stall_dump_test.txt";
    std::remove(file.c_str());

    // A thread that is inside a system call when the dump happens: this is the exact state that
    // broke the frame-pointer walk, and it has to keep working (a frame from this exe must show).
    std::atomic<bool> started{false};
    std::atomic<bool> stop{false};
    std::thread worker{[&] {
        started = true;
        while (!stop) {
            Sleep(1);
        }
    }};
    for (int waited = 0; waited < 2000 && !started; ++waited) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Give it time to get into the wait, not just to be created.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // The dump writes to stdout with printf; redirect fd 1 to a file to read what it printed. The
    // low-level redirect (not freopen) keeps stdout itself intact for the final message.
    std::fflush(stdout);
    const int saved_fd = _dup(1);
    FILE* target = std::fopen(file.c_str(), "wb");
    if (saved_fd < 0 || !target) {
        if (target) {
            std::fclose(target);
        }
        if (saved_fd >= 0) {
            _close(saved_fd);
        }
        stop = true;
        worker.join();
        std::printf("stall-dump-test: cannot redirect stdout\n");
        return 1;
    }
    _dup2(_fileno(target), 1);
    bb_win32_dump_threads(0, 0);
    std::fflush(stdout);
    _dup2(saved_fd, 1);
    _close(saved_fd);
    std::fclose(target);
    stop = true;
    worker.join();

    const std::string text = Slurp(file.c_str());
    std::remove(file.c_str());
    if (text.empty()) {
        std::printf("stall-dump-test: the dump wrote nothing\n");
        return 1;
    }

    const bool has_header = text.find("thread dump:") != std::string::npos;
    const bool has_thread_line = text.find("  thread ") != std::string::npos;
    // The frame chain of the sleeping worker is ntdll -> ... -> this exe, and the exe is what the
    // walk used to miss entirely.
    const bool has_exe_frame = text.find(" <- exe+0x") != std::string::npos;
    const bool names_module = text.find(".dll+0x") != std::string::npos;
    if (!has_header || !has_thread_line) {
        std::printf("stall-dump-test: FAIL -- no thread list in the dump\n%s\n", text.c_str());
        return 1;
    }
    if (!has_exe_frame) {
        std::printf("stall-dump-test: FAIL -- no frame of this exe in any chain (%s)\n%s\n",
                    "the walk is back to frame pointers only", text.c_str());
        return 1;
    }
    if (!names_module) {
        std::printf("stall-dump-test: FAIL -- addresses outside the exe are not named by module\n%s\n",
                    text.c_str());
        return 1;
    }
    std::printf("stall-dump-test: OK (%u bytes, chains reach this exe)\n", unsigned(text.size()));
    return 0;
}
