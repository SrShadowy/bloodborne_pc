// SPDX-License-Identifier: GPL-2.0-or-later
#include "mem_editor.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace Debugger {

static const char* DataTypeToString(DataType type) {
    switch (type) {
    case DataType::U8: return "u8";
    case DataType::U16: return "u16";
    case DataType::U32: return "u32";
    case DataType::U64: return "u64";
    case DataType::I8: return "i8";
    case DataType::I16: return "i16";
    case DataType::I32: return "i32";
    case DataType::I64: return "i64";
    case DataType::Float: return "float";
    case DataType::Double: return "double";
    }
    return "u32";
}

static DataType StringToDataType(const std::string& str) {
    if (str == "u8") return DataType::U8;
    if (str == "u16") return DataType::U16;
    if (str == "u32") return DataType::U32;
    if (str == "u64") return DataType::U64;
    if (str == "i8") return DataType::I8;
    if (str == "i16") return DataType::I16;
    if (str == "i32") return DataType::I32;
    if (str == "i64") return DataType::I64;
    if (str == "float" || str == "Float") return DataType::Float;
    if (str == "double" || str == "Double") return DataType::Double;
    return DataType::U32;
}

static std::string ExtractJsonString(const std::string& block, const std::string& key) {
    size_t pos = block.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = block.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = block.find('"', pos);
    if (pos == std::string::npos) return "";
    size_t end = block.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return block.substr(pos + 1, end - pos - 1);
}

static bool ExtractJsonBool(const std::string& block, const std::string& key, bool default_val = false) {
    size_t pos = block.find("\"" + key + "\"");
    if (pos == std::string::npos) return default_val;
    pos = block.find(':', pos);
    if (pos == std::string::npos) return default_val;
    size_t t_pos = block.find("true", pos);
    size_t f_pos = block.find("false", pos);
    size_t comma = block.find_first_of(",}\n", pos);
    if (t_pos != std::string::npos && t_pos < comma) return true;
    if (f_pos != std::string::npos && f_pos < comma) return false;
    return default_val;
}

static double ExtractJsonNumber(const std::string& block, const std::string& key, double default_val = 0.0) {
    size_t pos = block.find("\"" + key + "\"");
    if (pos == std::string::npos) return default_val;
    pos = block.find(':', pos);
    if (pos == std::string::npos) return default_val;
    pos = block.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos) return default_val;
    char* endptr = nullptr;
    double val = std::strtod(&block[pos], &endptr);
    return (endptr != &block[pos]) ? val : default_val;
}

static std::vector<int64_t> ExtractJsonOffsets(const std::string& block) {
    std::vector<int64_t> offsets;
    size_t pos = block.find("\"offsets\"");
    if (pos == std::string::npos) return offsets;
    pos = block.find('[', pos);
    if (pos == std::string::npos) return offsets;
    size_t end = block.find(']', pos);
    if (end == std::string::npos) return offsets;

    std::string arr = block.substr(pos + 1, end - pos - 1);
    size_t idx = 0;
    while (idx < arr.size()) {
        idx = arr.find_first_not_of(" \t\r\n,\"", idx);
        if (idx == std::string::npos) break;
        char* endptr = nullptr;
        int64_t val = std::strtoll(&arr[idx], &endptr, 0);
        if (endptr != &arr[idx]) {
            offsets.push_back(val);
            idx += (endptr - &arr[idx]);
        } else {
            idx++;
        }
    }
    return offsets;
}

static void EnsureDirectoryExists(const std::string& path) {
    size_t last_slash = path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        std::string dir = path.substr(0, last_slash);
        std::string cmd = "mkdir -p \"" + dir + "\"";
        (void)system(cmd.c_str());
    }
}

MemoryEditor::MemoryEditor() {
    LoadTableBBT();
}

MemoryEditor& MemoryEditor::Get() {
    static MemoryEditor instance;
    return instance;
}

std::string MemoryEditor::GetDefaultTablePath() {
    const char* data_dir = std::getenv("BB_DATA_DIR");
    if (data_dir && data_dir[0]) {
        return std::string(data_dir) + "/cheats/default.bbt";
    }
    return "user/cheats/default.bbt";
}

uintptr_t MemoryEditor::ResolveAddress(const WatchEntry& entry) const {
    if (!entry.is_pointer) {
        return entry.address;
    }
    if (entry.address == 0) {
        return 0;
    }
    uintptr_t curr = entry.address;
    for (size_t i = 0; i < entry.offsets.size(); ++i) {
        uintptr_t next_ptr = 0;
        if (!MemoryScanner::ReadMemory(curr, &next_ptr, sizeof(next_ptr)) || next_ptr == 0) {
            return 0; // Null or unmapped pointer
        }
        curr = static_cast<uintptr_t>(static_cast<int64_t>(next_ptr) + entry.offsets[i]);
    }
    return curr;
}

uintptr_t MemoryEditor::GetResolvedAddress(size_t index) const {
    std::lock_guard<std::mutex> lock(mutex);
    if (index >= entries.size()) return 0;
    return ResolveAddress(entries[index]);
}

void MemoryEditor::AddWatch(const std::string& label, uintptr_t address, DataType type) {
    std::lock_guard<std::mutex> lock(mutex);
    double cur_val = 0;
    std::string str;
    MemoryScanner::ReadFormatted(address, type, cur_val, str);
    entries.push_back({label, address, type, false, cur_val, false, {}});
}

void MemoryEditor::AddPointerWatch(const std::string& label, uintptr_t base_address, const std::vector<int64_t>& offsets, DataType type) {
    std::lock_guard<std::mutex> lock(mutex);
    WatchEntry entry;
    entry.label = label;
    entry.address = base_address;
    entry.type = type;
    entry.frozen = false;
    entry.is_pointer = true;
    entry.offsets = offsets;

    double cur_val = 0;
    std::string str;
    const uintptr_t resolved = ResolveAddress(entry);
    if (resolved != 0) {
        MemoryScanner::ReadFormatted(resolved, type, cur_val, str);
    }
    entry.freeze_value = cur_val;
    entries.push_back(entry);
}

void MemoryEditor::RemoveWatch(size_t index) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index < entries.size()) {
        entries.erase(entries.begin() + index);
    }
}

void MemoryEditor::Clear() {
    std::lock_guard<std::mutex> lock(mutex);
    entries.clear();
}

size_t MemoryEditor::GetCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return entries.size();
}

std::vector<WatchEntry> MemoryEditor::GetEntries() const {
    std::lock_guard<std::mutex> lock(mutex);
    return entries;
}

void MemoryEditor::SetFrozen(size_t index, bool frozen) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index < entries.size()) {
        entries[index].frozen = frozen;
    }
}

void MemoryEditor::SetFreezeValue(size_t index, double value) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index < entries.size()) {
        entries[index].freeze_value = value;
    }
}

void MemoryEditor::SetLabel(size_t index, const std::string& label) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index < entries.size()) {
        entries[index].label = label;
    }
}

void MemoryEditor::SetType(size_t index, DataType type) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index < entries.size()) {
        entries[index].type = type;
    }
}

void MemoryEditor::SetOffsets(size_t index, const std::vector<int64_t>& offsets) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index < entries.size()) {
        entries[index].offsets = offsets;
    }
}

bool MemoryEditor::WriteValue(size_t index, double value) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index >= entries.size()) return false;
    entries[index].freeze_value = value;
    const uintptr_t target = ResolveAddress(entries[index]);
    if (target == 0) return false;
    return MemoryScanner::WriteFormatted(target, entries[index].type, value);
}

void MemoryEditor::TickFreeze() {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& entry : entries) {
        if (entry.frozen) {
            const uintptr_t target = ResolveAddress(entry);
            if (target != 0) {
                MemoryScanner::WriteFormatted(target, entry.type, entry.freeze_value);
            }
        }
    }
}

bool MemoryEditor::SaveTableBBT(const std::string& filepath) const {
    const std::string path = filepath.empty() ? GetDefaultTablePath() : filepath;
    EnsureDirectoryExists(path);

    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;

    std::lock_guard<std::mutex> lock(mutex);
    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"format\": \"BBT\",\n");
    std::fprintf(f, "  \"version\": 1,\n");
    std::fprintf(f, "  \"game\": \"Bloodborne\",\n");
    std::fprintf(f, "  \"app_version\": \"01.09\",\n");
    std::fprintf(f, "  \"entries\": [\n");

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        std::fprintf(f, "    {\n");

        std::string escaped_label;
        for (char c : e.label) {
            if (c == '"') escaped_label += "\\\"";
            else if (c == '\\') escaped_label += "\\\\";
            else escaped_label += c;
        }
        std::fprintf(f, "      \"label\": \"%s\",\n", escaped_label.c_str());
        std::fprintf(f, "      \"address\": \"0x%llx\",\n", static_cast<unsigned long long>(e.address));
        std::fprintf(f, "      \"type\": \"%s\",\n", DataTypeToString(e.type));
        std::fprintf(f, "      \"is_pointer\": %s,\n", e.is_pointer ? "true" : "false");

        std::fprintf(f, "      \"offsets\": [");
        for (size_t j = 0; j < e.offsets.size(); ++j) {
            std::fprintf(f, "\"0x%llx\"%s", static_cast<unsigned long long>(e.offsets[j]), (j + 1 < e.offsets.size()) ? ", " : "");
        }
        std::fprintf(f, "],\n");

        std::fprintf(f, "      \"frozen\": %s,\n", e.frozen ? "true" : "false");
        std::fprintf(f, "      \"freeze_value\": %.6f\n", e.freeze_value);
        std::fprintf(f, "    }%s\n", (i + 1 < entries.size()) ? "," : "");
    }

    std::fprintf(f, "  ]\n");
    std::fprintf(f, "}\n");
    std::fclose(f);
    return true;
}

bool MemoryEditor::LoadTableBBT(const std::string& filepath) {
    const std::string path = filepath.empty() ? GetDefaultTablePath() : filepath;
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (content.empty()) return false;

    std::vector<WatchEntry> loaded_entries;
    size_t entries_pos = content.find("\"entries\"");
    if (entries_pos != std::string::npos) {
        size_t cur = content.find('[', entries_pos);
        while (cur != std::string::npos) {
            size_t obj_start = content.find('{', cur);
            if (obj_start == std::string::npos) break;

            int depth = 0;
            size_t obj_end = std::string::npos;
            for (size_t i = obj_start; i < content.size(); ++i) {
                if (content[i] == '{') depth++;
                else if (content[i] == '}') {
                    depth--;
                    if (depth == 0) {
                        obj_end = i;
                        break;
                    }
                }
            }
            if (obj_end == std::string::npos) break;

            std::string block = content.substr(obj_start, obj_end - obj_start + 1);
            WatchEntry e;
            e.label = ExtractJsonString(block, "label");
            std::string addr_str = ExtractJsonString(block, "address");
            if (!addr_str.empty()) {
                e.address = std::strtoull(addr_str.c_str(), nullptr, 0);
            } else {
                e.address = static_cast<uintptr_t>(ExtractJsonNumber(block, "address"));
            }
            std::string type_str = ExtractJsonString(block, "type");
            e.type = StringToDataType(type_str);
            e.is_pointer = ExtractJsonBool(block, "is_pointer", false);
            e.frozen = ExtractJsonBool(block, "frozen", false);
            e.freeze_value = ExtractJsonNumber(block, "freeze_value", 0.0);
            e.offsets = ExtractJsonOffsets(block);

            loaded_entries.push_back(e);

            cur = obj_end + 1;
            size_t next_bracket = content.find_first_of("]{", cur);
            if (next_bracket != std::string::npos && content[next_bracket] == ']') break;
        }
    }

    std::lock_guard<std::mutex> lock(mutex);
    entries = std::move(loaded_entries);
    return true;
}

} // namespace Debugger

