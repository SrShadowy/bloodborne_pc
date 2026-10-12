// SPDX-License-Identifier: GPL-2.0-or-later
#include "mem_scanner.h"
#include "pattern_scanner.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

namespace Debugger {

size_t MemoryScanner::GetTypeSize(DataType type) {
    switch (type) {
    case DataType::U8:
    case DataType::I8: return 1;
    case DataType::U16:
    case DataType::I16: return 2;
    case DataType::U32:
    case DataType::I32:
    case DataType::Float: return 4;
    case DataType::U64:
    case DataType::I64:
    case DataType::Double:
    case DataType::Pattern: return 8;
    }
    return 4;
}

const char* MemoryScanner::GetTypeName(DataType type) {
    switch (type) {
    case DataType::U8: return "1 Byte (u8)";
    case DataType::I8: return "1 Byte (i8)";
    case DataType::U16: return "2 Bytes (u16)";
    case DataType::I16: return "2 Bytes (i16)";
    case DataType::U32: return "4 Bytes (u32)";
    case DataType::I32: return "4 Bytes (i32)";
    case DataType::U64: return "8 Bytes (u64)";
    case DataType::I64: return "8 Bytes (i64)";
    case DataType::Float: return "Float";
    case DataType::Double: return "Double";
    case DataType::Pattern: return "Array of Bytes (AoB)";
    }
    return "Unknown";
}

double MemoryScanner::DecodeValue(DataType type, const void* ptr) {
    switch (type) {
    case DataType::U8: return *reinterpret_cast<const uint8_t*>(ptr);
    case DataType::I8: return *reinterpret_cast<const int8_t*>(ptr);
    case DataType::U16: return *reinterpret_cast<const uint16_t*>(ptr);
    case DataType::I16: return *reinterpret_cast<const int16_t*>(ptr);
    case DataType::U32: return *reinterpret_cast<const uint32_t*>(ptr);
    case DataType::I32: return *reinterpret_cast<const int32_t*>(ptr);
    case DataType::U64: return static_cast<double>(*reinterpret_cast<const uint64_t*>(ptr));
    case DataType::I64: return static_cast<double>(*reinterpret_cast<const int64_t*>(ptr));
    case DataType::Float: return *reinterpret_cast<const float*>(ptr);
    case DataType::Double: return *reinterpret_cast<const double*>(ptr);
    case DataType::Pattern: return 0.0;
    }
    return 0.0;
}

uint64_t MemoryScanner::EncodeRaw(DataType type, const void* ptr) {
    uint64_t raw = 0;
    std::memcpy(&raw, ptr, std::min(sizeof(raw), MemoryScanner::GetTypeSize(type)));
    return raw;
}

bool MemoryScanner::ReadFormatted(uintptr_t addr, DataType type, double& out_num, std::string& out_str) {
    if (type == DataType::Pattern) {
        size_t plen = Get().GetCurrentPatternLength();
        if (plen == 0) plen = 8;
        plen = std::min(plen, static_cast<size_t>(32));
        uint8_t pbuf[32] = {0};
        if (!ReadMemory(addr, pbuf, plen)) {
            out_str = "???";
            return false;
        }
        out_num = 0;
        std::memcpy(&out_num, pbuf, std::min(sizeof(out_num), plen));
        out_str = PatternScanner::FormatBytes(pbuf, plen);
        return true;
    }
    uint8_t buffer[8] = {0};
    const size_t sz = GetTypeSize(type);
    if (!ReadMemory(addr, buffer, sz)) {
        out_str = "???";
        return false;
    }
    out_num = DecodeValue(type, buffer);
    char tmp[64];
    switch (type) {
    case DataType::U8: std::snprintf(tmp, sizeof(tmp), "%u", *reinterpret_cast<uint8_t*>(buffer)); break;
    case DataType::I8: std::snprintf(tmp, sizeof(tmp), "%d", *reinterpret_cast<int8_t*>(buffer)); break;
    case DataType::U16: std::snprintf(tmp, sizeof(tmp), "%u", *reinterpret_cast<uint16_t*>(buffer)); break;
    case DataType::I16: std::snprintf(tmp, sizeof(tmp), "%d", *reinterpret_cast<int16_t*>(buffer)); break;
    case DataType::U32: std::snprintf(tmp, sizeof(tmp), "%u", *reinterpret_cast<uint32_t*>(buffer)); break;
    case DataType::I32: std::snprintf(tmp, sizeof(tmp), "%d", *reinterpret_cast<int32_t*>(buffer)); break;
    case DataType::U64: std::snprintf(tmp, sizeof(tmp), "%llu", static_cast<unsigned long long>(*reinterpret_cast<uint64_t*>(buffer))); break;
    case DataType::I64: std::snprintf(tmp, sizeof(tmp), "%lld", static_cast<long long>(*reinterpret_cast<int64_t*>(buffer))); break;
    case DataType::Float: std::snprintf(tmp, sizeof(tmp), "%.3f", *reinterpret_cast<float*>(buffer)); break;
    case DataType::Double: std::snprintf(tmp, sizeof(tmp), "%.4f", *reinterpret_cast<double*>(buffer)); break;
    case DataType::Pattern: break;
    }
    out_str = tmp;
    return true;
}

bool MemoryScanner::WriteFormatted(uintptr_t addr, DataType type, double num) {
    uint8_t buffer[8] = {0};
    const size_t sz = GetTypeSize(type);
    switch (type) {
    case DataType::U8: *reinterpret_cast<uint8_t*>(buffer) = static_cast<uint8_t>(num); break;
    case DataType::I8: *reinterpret_cast<int8_t*>(buffer) = static_cast<int8_t>(num); break;
    case DataType::U16: *reinterpret_cast<uint16_t*>(buffer) = static_cast<uint16_t>(num); break;
    case DataType::I16: *reinterpret_cast<int16_t*>(buffer) = static_cast<int16_t>(num); break;
    case DataType::U32: *reinterpret_cast<uint32_t*>(buffer) = static_cast<uint32_t>(num); break;
    case DataType::I32: *reinterpret_cast<int32_t*>(buffer) = static_cast<int32_t>(num); break;
    case DataType::U64: *reinterpret_cast<uint64_t*>(buffer) = static_cast<uint64_t>(num); break;
    case DataType::I64: *reinterpret_cast<int64_t*>(buffer) = static_cast<int64_t>(num); break;
    case DataType::Float: *reinterpret_cast<float*>(buffer) = static_cast<float>(num); break;
    case DataType::Double: *reinterpret_cast<double*>(buffer) = num; break;
    case DataType::Pattern: return false;
    }
    return WriteMemory(addr, buffer, sz);
}

} // namespace Debugger
