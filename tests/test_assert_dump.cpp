// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// [extra] F0a (DEBATE #3): assert_dump.h — artifact path is <dir>/<sanitized site>/<hash>-<perm>.bin,
// one artifact per (site, hash, perm). No GPU; runs anywhere. ninja -C out/win-dev assert-dump-test.
#include "render/assert_dump.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

std::vector<u8> Slurp(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "bb_assert_dump_test";
    fs::remove_all(dir); // a previous run's leftovers read as this run's writes

    // Off: nothing is written, not even the directory.
    SetEnv("BB_ASSERT_DUMP", "");
    assert(!BbAssertDump::Enabled());
    BbAssertDump::Write("is_flat", 0x1, 0, std::vector<u8>{{9, 9}});
    assert(!fs::exists(dir));

    SetEnv("BB_ASSERT_DUMP", dir.string().c_str());
    assert(BbAssertDump::Enabled());

    // The check's text minus the throw's fixed prefix names the directory (assert.cpp).
    const std::vector<u8> first{{1, 2, 3, 4}};
    BbAssertDump::Write("assertion failed: is_flat", 0xAABBCCDDEEFF0011ULL, 3, first);
    const fs::path flat = dir / "is_flat" / "aabbccddeeff0011-3.bin";
    assert(fs::exists(flat));
    assert(Slurp(flat) == first);

    // Dedup by name: a later failure of the same (site, hash, perm) keeps the first record.
    BbAssertDump::Write("assertion failed: is_flat", 0xAABBCCDDEEFF0011ULL, 3,
                        std::vector<u8>{{9, 9, 9}});
    assert(Slurp(flat) == first);

    // Anything outside [A-Za-z0-9._-] folds to '_': "!(IsSharpSource(x))" -> "__IsSharpSource_x__".
    BbAssertDump::Write("assertion failed: !(IsSharpSource(x))", 0x22, 0,
                        std::vector<u8>{u8{5}});
    assert(fs::exists(dir / "__IsSharpSource_x__" / "0000000000000022-0.bin"));

    // No site known (a formatted ASSERT_MSG): the "unknown" bucket.
    BbAssertDump::Write(nullptr, 0x33, 0, std::vector<u8>{u8{6}});
    assert(fs::exists(dir / "unknown" / "0000000000000033-0.bin"));

    // The throw without a site ("assertion failed") also keeps a readable name.
    BbAssertDump::Write("assertion failed", 0x44, 0, std::vector<u8>{u8{7}});
    assert(fs::exists(dir / "assertion_failed" / "0000000000000044-0.bin"));

    SetEnv("BB_ASSERT_DUMP", "");
    fs::remove_all(dir);
    std::puts("assert-dump-test: OK");
    return 0;
}
