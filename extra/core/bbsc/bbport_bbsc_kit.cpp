// SPDX-License-Identifier: GPL-2.0-or-later

#include "bbport_bbsc_kit.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include <sys/uio.h>
#include <unistd.h>

extern "C" int runtime_memory_region(std::uintptr_t address, std::uintptr_t* start,
                                     std::uintptr_t* end, int* mapped);
extern "C" void runtime_memory_read_backing(std::uintptr_t address, void* data,
                                            std::uint64_t size);

extern "C" const char* extra_bbsc_save_root(void) {
    const char* dir = std::getenv("BB_GPU_USER_DIR");
    if (dir == nullptr || dir[0] == '\0') {
        dir = std::getenv("BB_USER_DIR");
    }
    return dir != nullptr && dir[0] != '\0' ? dir : nullptr;
}

extern "C" void extra_bbsc_setup_save_dir(void) {
    if (std::getenv("BBSC_SAVE_DIR")) {
        return;
    }
    const char* user = extra_bbsc_save_root();
    const std::filesystem::path save_root =
        std::filesystem::path(user && user[0] ? user : "user") / "savedata";
    std::error_code error;
    for (const auto& id : std::filesystem::directory_iterator(save_root, error)) {
        std::error_code title_ec;
        for (const auto& title : std::filesystem::directory_iterator(id.path(), title_ec)) {
            const auto folder = title.path() / "SPRJ0005";
            if (std::filesystem::is_directory(folder, error)) {
                setenv("BBSC_SAVE_DIR", folder.c_str(), 0);
                std::printf("BBSC: save folder %s\n", folder.c_str());
                return;
            }
        }
    }
}

extern "C" void extra_bbsc_bell_scan_forever(std::atomic<bool>* ready, void (*prepare)(void)) {
    if (ready == nullptr || prepare == nullptr) {
        return;
    }
    while (!ready->load(std::memory_order_acquire)) {
        prepare();
        if (ready->load(std::memory_order_acquire)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

extern "C" bool extra_bbsc_check_mapping(const char* param, const char* line) {
    if (param == nullptr || line == nullptr) {
        return false;
    }

    if (param[1] != 'w' || std::strstr(line, ".so") != nullptr) {
        return false;
    }

    return true;
}

extern "C" bool extra_safe_read(void* dst, const void* src, size_t size) {
    if (dst == nullptr || src == nullptr || size == 0) {
        return false;
    }
    std::memcpy(dst, src, size);
    return true;
}

extern "C" void extra_bbsc_find_in_memory(
    const std::uint8_t* needle_ptr, std::size_t needle_len,
    std::uint64_t lo, std::uint64_t hi, std::size_t max_hits,
    std::vector<std::uint64_t>* hits) {
    if (!hits || !needle_ptr || needle_len == 0 || lo >= hi) {
        return;
    }

    constexpr std::size_t Piece = 1u << 20;
    std::vector<std::uint8_t> buffer(Piece + needle_len);
    constexpr std::uint64_t GuestMin = 0x1000000000ULL;

    auto match_chunk = [&](std::uint64_t base, std::size_t len) {
        const std::uint8_t* data = buffer.data();
        const std::uint8_t* last = data + len - needle_len;
        for (const std::uint8_t* p = data; p <= last && hits->size() < max_hits; ++p) {
            p = static_cast<const std::uint8_t*>(
                std::memchr(p, needle_ptr[0], std::size_t(last - p) + 1));
            if (!p) {
                break;
            }
            if (std::size_t(p - data) < Piece &&
                std::memcmp(p, needle_ptr, needle_len) == 0) {
                hits->push_back(base + std::uint64_t(p - data));
            }
        }
    };

    // 1. Low mappings below GuestMin (e.g. eboot image): readable mappings from /proc/self/maps
    if (lo < GuestMin) {
        const std::uint64_t low_hi = std::min<std::uint64_t>(hi, GuestMin);
        if (FILE* maps = std::fopen("/proc/self/maps", "r")) {
            char line[512];
            while (hits->size() < max_hits && std::fgets(line, sizeof(line), maps)) {
                unsigned long long start = 0, end = 0;
                char perms[5] = {};
                if (std::sscanf(line, "%llx-%llx %4s", &start, &end, perms) != 3 || perms[0] != 'r') {
                    continue;
                }
                if (!extra_bbsc_check_mapping(perms, line)) {
                    continue;
                }
                start = std::max<unsigned long long>(start, lo);
                end = std::min<unsigned long long>(end, low_hi);
                for (std::uint64_t at = start; at < end && hits->size() < max_hits; at += Piece) {
                    const std::size_t want =
                        std::size_t(std::min<std::uint64_t>(end - at, Piece + needle_len - 1));
                    iovec local{buffer.data(), want};
                    iovec remote{reinterpret_cast<void*>(at), want};
                    ssize_t got = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
                    if (got < 0 && extra_safe_read(buffer.data(), reinterpret_cast<const void*>(at), want)) {
                        got = static_cast<ssize_t>(want);
                    }
                    if (got < ssize_t(needle_len)) {
                        continue;
                    }
                    match_chunk(at, std::size_t(got));
                }
            }
            std::fclose(maps);
        }
    }

    // 2. Guest mappings at or above GuestMin: query runtime_memory
    std::uint64_t at = std::max<std::uint64_t>(lo, GuestMin);
    while (at < hi && hits->size() < max_hits) {
        std::uintptr_t r_start = 0, r_end = 0;
        int mapped = 0;
        if (!runtime_memory_region(at, &r_start, &r_end, &mapped) || r_end <= at) {
            break;
        }
        if (!mapped) {
            at = r_end;
            continue;
        }
        const std::uint64_t start = std::max<std::uint64_t>(r_start, at);
        const std::uint64_t end = std::min<std::uint64_t>(r_end, hi);
        for (std::uint64_t chunk = start; chunk < end && hits->size() < max_hits; chunk += Piece) {
            const std::size_t want =
                std::size_t(std::min<std::uint64_t>(end - chunk, Piece + needle_len - 1));
            if (want < needle_len) {
                break;
            }
            runtime_memory_read_backing(chunk, buffer.data(), want);
            match_chunk(chunk, want);
        }
        at = r_end;
    }
}
