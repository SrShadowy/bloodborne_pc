// SPDX-License-Identifier: GPL-2.0-or-later
#include "assembler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <algorithm>

namespace UI {

static std::string TrimAndLower(const std::string& str) {
    std::string s;
    for (char c : str) {
        if (!std::isspace(static_cast<unsigned char>(c))) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (!s.empty() && s.back() != ' ') s += ' ';
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

static bool FastPathAssemble(const std::string& clean, uintptr_t target_addr, std::vector<uint8_t>& out_bytes) {
    if (clean == "nop") { out_bytes = {0x90}; return true; }
    if (clean == "ret" || clean == "ret 0") { out_bytes = {0xC3}; return true; }
    if (clean == "int3") { out_bytes = {0xCC}; return true; }

    // Fast-path common xor zeroing
    if (clean == "xor eax, eax") { out_bytes = {0x31, 0xC0}; return true; }
    if (clean == "xor rax, rax") { out_bytes = {0x48, 0x31, 0xC0}; return true; }
    if (clean == "xor ecx, ecx") { out_bytes = {0x31, 0xC9}; return true; }
    if (clean == "xor rcx, rcx") { out_bytes = {0x48, 0x31, 0xC9}; return true; }
    if (clean == "xor edx, edx") { out_bytes = {0x31, 0xD2}; return true; }
    if (clean == "xor rdx, rdx") { out_bytes = {0x48, 0x31, 0xD2}; return true; }
    if (clean == "xor ebx, ebx") { out_bytes = {0x31, 0xDB}; return true; }
    if (clean == "xor rbx, rbx") { out_bytes = {0x48, 0x31, 0xDB}; return true; }
    if (clean == "xor esi, esi") { out_bytes = {0x31, 0xF6}; return true; }
    if (clean == "xor rsi, rsi") { out_bytes = {0x48, 0x31, 0xF6}; return true; }
    if (clean == "xor edi, edi") { out_bytes = {0x31, 0xFF}; return true; }
    if (clean == "xor rdi, rdi") { out_bytes = {0x48, 0x31, 0xFF}; return true; }

    // Fast-path simple mov imm
    if (clean == "mov eax, 0") { out_bytes = {0xB8, 0x00, 0x00, 0x00, 0x00}; return true; }
    if (clean == "mov eax, 1") { out_bytes = {0xB8, 0x01, 0x00, 0x00, 0x00}; return true; }
    if (clean == "mov rax, 0") { out_bytes = {0x48, 0xC7, 0xC0, 0x00, 0x00, 0x00, 0x00}; return true; }
    if (clean == "mov rax, 1") { out_bytes = {0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00}; return true; }

    // Fast-path relative jump/call to hex address
    if (clean.rfind("jmp 0x", 0) == 0 && target_addr != 0) {
        uintptr_t dest = std::strtoull(clean.c_str() + 6, nullptr, 16);
        int64_t diff = static_cast<int64_t>(dest) - static_cast<int64_t>(target_addr + 5);
        if (diff >= -2147483648LL && diff <= 2147483647LL) {
            int32_t rel = static_cast<int32_t>(diff);
            out_bytes.resize(5);
            out_bytes[0] = 0xE9;
            std::memcpy(out_bytes.data() + 1, &rel, sizeof(rel));
            return true;
        }
    }
    if (clean.rfind("call 0x", 0) == 0 && target_addr != 0) {
        uintptr_t dest = std::strtoull(clean.c_str() + 7, nullptr, 16);
        int64_t diff = static_cast<int64_t>(dest) - static_cast<int64_t>(target_addr + 5);
        if (diff >= -2147483648LL && diff <= 2147483647LL) {
            int32_t rel = static_cast<int32_t>(diff);
            out_bytes.resize(5);
            out_bytes[0] = 0xE8;
            std::memcpy(out_bytes.data() + 1, &rel, sizeof(rel));
            return true;
        }
    }

    return false;
}

bool Assembler::Assemble(const std::string& asm_text, uintptr_t target_addr,
                         std::vector<uint8_t>& out_bytes, std::string& out_error) {
    out_bytes.clear();
    out_error.clear();

    const std::string clean = TrimAndLower(asm_text);
    if (clean.empty()) {
        out_error = "Empty instruction";
        return false;
    }

    if (FastPathAssemble(clean, target_addr, out_bytes)) {
        return true;
    }

    // Sanitize input for shell pipe (allow only alphanumeric, spaces, commas, brackets, +, -, *, /, .)
    for (char c : asm_text) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && !std::isspace(static_cast<unsigned char>(c)) &&
            c != ',' && c != '[' && c != ']' && c != '+' && c != '-' && c != '*' && c != '.' && c != ':' && c != '_') {
            out_error = "Invalid character in assembly string";
            return false;
        }
    }

    char cmd[512];
    std::snprintf(cmd, sizeof(cmd), "printf '%%s\\n' \".intel_syntax noprefix\" \"%s\" | llvm-mc -triple=x86_64 -show-encoding 2>&1",
                  asm_text.c_str());

    FILE* fp = popen(cmd, "r");
    if (!fp) {
        out_error = "Failed to launch llvm-mc assembler";
        return false;
    }

    char line[512];
    std::string full_output;
    bool found_encoding = false;

    while (std::fgets(line, sizeof(line), fp)) {
        full_output += line;
        const char* enc = std::strstr(line, "# encoding: [");
        if (enc) {
            enc += 13;
            found_encoding = true;
            while (*enc && *enc != ']') {
                if (enc[0] == '0' && (enc[1] == 'x' || enc[1] == 'X')) {
                    uint8_t byte = static_cast<uint8_t>(std::strtoul(enc, nullptr, 16));
                    out_bytes.push_back(byte);
                    enc += 2;
                    while (std::isxdigit(static_cast<unsigned char>(*enc))) enc++;
                } else {
                    enc++;
                }
            }
        }
    }
    pclose(fp);

    if (found_encoding && !out_bytes.empty()) {
        return true;
    }

    const size_t err_pos = full_output.find("error:");
    if (err_pos != std::string::npos) {
        size_t end_line = full_output.find('\n', err_pos);
        out_error = full_output.substr(err_pos, end_line - err_pos);
    } else {
        out_error = "Assembly compilation failed";
    }
    return false;
}

} // namespace UI
