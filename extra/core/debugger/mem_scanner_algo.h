// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "mem_scanner.h"
#include <cmath>
#include <cstring>
#include <vector>

namespace Debugger {

template <typename T>
static inline void ScanExactTyped(const uint8_t* data, size_t size, uintptr_t base_addr, double target_val,
                                  std::vector<ScanMatch>& out, size_t max, bool& hit) {
    const T target = static_cast<T>(target_val);
    const size_t count = size / sizeof(T);
    const T* items = reinterpret_cast<const T*>(data);
    for (size_t i = 0; i < count; ++i) {
        if (items[i] == target) {
            uint64_t raw = 0;
            std::memcpy(&raw, &items[i], sizeof(T));
            out.push_back({base_addr + i * sizeof(T), raw, raw});
            if (out.size() >= max) { hit = true; return; }
        }
    }
}

static inline void ScanExactFloat(const uint8_t* data, size_t size, uintptr_t base_addr, float target,
                                  std::vector<ScanMatch>& out, size_t max, bool& hit) {
    const size_t count = size / sizeof(float);
    const float* items = reinterpret_cast<const float*>(data);
    for (size_t i = 0; i < count; ++i) {
        if (std::fabs(items[i] - target) <= 0.001f) {
            uint64_t raw = 0;
            std::memcpy(&raw, &items[i], sizeof(float));
            out.push_back({base_addr + i * sizeof(float), raw, raw});
            if (out.size() >= max) { hit = true; return; }
        }
    }
}

static inline void ScanExactDouble(const uint8_t* data, size_t size, uintptr_t base_addr, double target,
                                   std::vector<ScanMatch>& out, size_t max, bool& hit) {
    const size_t count = size / sizeof(double);
    const double* items = reinterpret_cast<const double*>(data);
    for (size_t i = 0; i < count; ++i) {
        if (std::fabs(items[i] - target) <= 0.001) {
            uint64_t raw = 0;
            std::memcpy(&raw, &items[i], sizeof(double));
            out.push_back({base_addr + i * sizeof(double), raw, raw});
            if (out.size() >= max) { hit = true; return; }
        }
    }
}

} // namespace Debugger
