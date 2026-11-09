// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// [extra] F0a (NextStep, DEBATE #3): see assert_dump.h.
#include "render/assert_dump.h"
#include "shim/bb_env.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace BbAssertDump {

namespace {

const std::filesystem::path Root() {
    // Read every call (cheap, vs. the milliseconds a compilation costs): a process can set
    // BB_ASSERT_DUMP after others, and tests flip it within one run.
    const char* env = bb_env("BB_ASSERT_DUMP");
    return env && *env ? std::filesystem::path{env} : std::filesystem::path{};
}

/// A directory-safe site: the failed check's text without the throw's fixed prefix
/// (common/assert.cpp), everything outside [A-Za-z0-9._-] folded to '_', capped in length.
std::string SiteName(const char* site) {
    std::string name = site && *site ? site : "unknown";
    const std::string prefix{"assertion failed: "};
    if (name.starts_with(prefix)) {
        name.erase(0, prefix.size());
    }
    for (char& ch : name) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
        if (!ok) {
            ch = '_';
        }
    }
    name.resize(std::min(name.size(), size_t{80}));
    return name.empty() ? "unknown" : name;
}

} // namespace

bool Enabled() {
    return !Root().empty();
}

void Write(const char* site, const u64 pgm_hash, const u64 perm_idx, std::span<const u8> bytes) {
    const auto root = Root();
    if (root.empty()) {
        return;
    }
    std::error_code ec;
    const std::filesystem::path dir = root / SiteName(site);
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return;
    }
    char name[64];
    std::snprintf(name, sizeof(name), "%016llx-%llu.bin", (unsigned long long)pgm_hash,
                  (unsigned long long)perm_idx);
    const std::filesystem::path path = dir / name;
    if (std::filesystem::exists(path, ec)) {
        return; // already on disk: the same offender fails the same way every run
    }
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    out.close();
    if (out) {
        std::fprintf(stderr, "[AssertDump] %s shader %#llx perm %llu -> %s (%zu bytes)\n",
                     SiteName(site).c_str(), (unsigned long long)pgm_hash,
                     (unsigned long long)perm_idx, path.string().c_str(), bytes.size());
        std::fflush(stderr);
    }
}

} // namespace BbAssertDump
