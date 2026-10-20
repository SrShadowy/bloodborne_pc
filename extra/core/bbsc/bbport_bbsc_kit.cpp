// SPDX-License-Identifier: GPL-2.0-or-later

#include "bbport_bbsc_kit.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

extern "C" const char* extra_bbsc_save_root(void) {
    const char* dir = std::getenv("BB_GPU_USER_DIR");
    return dir != nullptr && dir[0] != '\0' ? dir : nullptr;
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
