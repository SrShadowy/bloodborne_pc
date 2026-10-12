// SPDX-License-Identifier: GPL-2.0-or-later
#include "assembler.h"
#include "pattern_scanner.h"
#include "mem_scanner.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <string>

int main() {
    printf("[TEST] Starting Assembler and PatternScanner unit tests...\n");

    // 1. Test Assembler - Fast Path
    {
        std::vector<uint8_t> bytes;
        std::string err;

        assert(UI::Assembler::Assemble("nop", 0x1000, bytes, err));
        assert(bytes.size() == 1 && bytes[0] == 0x90);

        assert(UI::Assembler::Assemble("ret", 0x1000, bytes, err));
        assert(bytes.size() == 1 && bytes[0] == 0xC3);

        assert(UI::Assembler::Assemble("xor eax, eax", 0x1000, bytes, err));
        assert(bytes.size() == 2 && bytes[0] == 0x31 && bytes[1] == 0xC0);

        assert(UI::Assembler::Assemble("xor rax, rax", 0x1000, bytes, err));
        assert(bytes.size() == 3 && bytes[0] == 0x48 && bytes[1] == 0x31 && bytes[2] == 0xC0);

        assert(UI::Assembler::Assemble("mov eax, 1", 0x1000, bytes, err));
        assert(bytes.size() == 5 && bytes[0] == 0xB8 && bytes[1] == 0x01);

        // Relative jump fast path
        assert(UI::Assembler::Assemble("jmp 0x1010", 0x1000, bytes, err));
        assert(bytes.size() == 5 && bytes[0] == 0xE9);
    }
    printf("[TEST] Assembler Fast-path tests passed.\n");

    // 2. Test Assembler - LLVM-MC Path
    {
        std::vector<uint8_t> bytes;
        std::string err;

        // Arbitrary complex x86-64 instruction
        assert(UI::Assembler::Assemble("mov qword ptr [rsp + 0x18], rax", 0x2000, bytes, err));
        assert(!bytes.empty());
        assert(bytes[0] == 0x48 && bytes[1] == 0x89);

        // Invalid assembly mnemonic
        bool ok = UI::Assembler::Assemble("notarealinstruction xyz, 123", 0x2000, bytes, err);
        assert(!ok);
        assert(!err.empty());
    }
    printf("[TEST] Assembler LLVM-MC tests passed.\n");

    // 3. Test PatternScanner - ParsePattern
    {
        std::vector<Debugger::PatternToken> tokens;

        // Space separated with ?? wildcards
        assert(Debugger::PatternScanner::ParsePattern("48 8B 05 ?? ?? ?? ?? 48 85 C0", tokens));
        assert(tokens.size() == 10);
        assert(tokens[0].value == 0x48 && !tokens[0].wildcard);
        assert(tokens[1].value == 0x8B && !tokens[1].wildcard);
        assert(tokens[2].value == 0x05 && !tokens[2].wildcard);
        assert(tokens[3].wildcard && tokens[4].wildcard && tokens[5].wildcard && tokens[6].wildcard);
        assert(tokens[7].value == 0x48 && !tokens[7].wildcard);
        assert(tokens[8].value == 0x85 && !tokens[8].wildcard);
        assert(tokens[9].value == 0xC0 && !tokens[9].wildcard);

        // Single '?' and '*' wildcards, no spaces
        assert(Debugger::PatternScanner::ParsePattern("90 ? * 90", tokens));
        assert(tokens.size() == 4);
        assert(tokens[0].value == 0x90 && !tokens[0].wildcard);
        assert(tokens[1].wildcard);
        assert(tokens[2].wildcard);
        assert(tokens[3].value == 0x90 && !tokens[3].wildcard);

        // Contiguous hex with ??
        assert(Debugger::PatternScanner::ParsePattern("488B????C3", tokens));
        assert(tokens.size() == 5);
        assert(tokens[0].value == 0x48 && !tokens[0].wildcard);
        assert(tokens[1].value == 0x8B && !tokens[1].wildcard);
        assert(tokens[2].wildcard && tokens[3].wildcard);
        assert(tokens[4].value == 0xC3 && !tokens[4].wildcard);

        // Invalid pattern
        assert(!Debugger::PatternScanner::ParsePattern("48 8B ZZ 00", tokens));
        assert(!Debugger::PatternScanner::ParsePattern("", tokens));
    }
    printf("[TEST] PatternScanner ParsePattern tests passed.\n");

    // 4. Test PatternScanner - ScanChunk & Fast Skip
    {
        std::vector<Debugger::PatternToken> tokens;
        assert(Debugger::PatternScanner::ParsePattern("48 8B ?? 10 90", tokens));

        // Create buffer with dummy data and needle
        std::vector<uint8_t> buffer(1024, 0xCC);
        // Inject match at offset 100
        buffer[100] = 0x48;
        buffer[101] = 0x8B;
        buffer[102] = 0x55; // wildcard
        buffer[103] = 0x10;
        buffer[104] = 0x90;

        // Inject second match at offset 500
        buffer[500] = 0x48;
        buffer[501] = 0x8B;
        buffer[502] = 0xAA; // wildcard
        buffer[503] = 0x10;
        buffer[504] = 0x90;

        std::vector<Debugger::ScanMatch> matches;
        Debugger::PatternScanner::ScanChunk(buffer.data(), buffer.size(), 0x10000, tokens, matches, 100);

        assert(matches.size() == 2);
        assert(matches[0].address == 0x10000 + 100);
        assert(matches[1].address == 0x10000 + 500);
    }
    printf("[TEST] PatternScanner ScanChunk tests passed.\n");

    // 5. Test FormatBytes
    {
        uint8_t sample[] = { 0x48, 0x89, 0x5C, 0x24 };
        std::string fmt = Debugger::PatternScanner::FormatBytes(sample, sizeof(sample));
        assert(fmt == "48 89 5C 24");
    }
    printf("[TEST] PatternScanner FormatBytes tests passed.\n");

    printf("[ALL TESTS PASSED SUCCESSFULLY!]\n");
    return 0;
}
