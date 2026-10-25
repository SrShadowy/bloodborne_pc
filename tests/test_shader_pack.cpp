// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "extra/core/shader_precompile/bbport_shader_pack.h"

int main() {
    std::printf("Testing BbShaderPack 3-file archive...\n");

    const std::filesystem::path test_dir = "out/test_shader_pack_dir";
    std::filesystem::remove_all(test_dir);
    std::filesystem::create_directories(test_dir);

    // Create mock .spv file
    {
        std::ofstream f(test_dir / "0x00000000006c079b_0.spv", std::ios::binary);
        const std::vector<uint32_t> spv = {0x07230203, 0x00010600, 0, 10, 0};
        f.write(reinterpret_cast<const char*>(spv.data()), spv.size() * sizeof(uint32_t));
    }
    // Create mock .meta file
    {
        std::ofstream f(test_dir / "0x00000000006c079b.meta", std::ios::binary);
        const std::vector<uint8_t> meta = {1, 2, 3, 4, 5, 6, 7, 8};
        f.write(reinterpret_cast<const char*>(meta.data()), meta.size());
    }
    // Create mock .key file
    {
        std::ofstream f(test_dir / "g_0x12345678abcdef01.key", std::ios::binary);
        const std::vector<uint8_t> key = {9, 10, 11, 12};
        f.write(reinterpret_cast<const char*>(key.data()), key.size());
    }

    assert(!BbShaderPack::HasArchive(test_dir));

    bool ok = BbShaderPack::PackDirectory(test_dir, "CUSA03173");
    assert(ok && "PackDirectory failed");

    assert(BbShaderPack::HasArchive(test_dir) && "HasArchive returned false after pack");

    const auto stats = BbShaderPack::GetArchiveStats(test_dir);
    assert(stats.valid && "Archive stats invalid");
    assert(stats.shader_count == 1);
    assert(stats.pipeline_count == 1);
    assert(stats.title_id == "CUSA03173");
    assert(stats.shaders_file_size > 0);
    assert(stats.meta_file_size > 0);
    assert(stats.index_file_size > 0);

    bool verified = BbShaderPack::VerifyArchive(test_dir);
    assert(verified && "VerifyArchive failed");

    std::printf("BbShaderPack test passed successfully! (Index: %zu B, Shaders: %zu B, Meta: %zu B)\n",
                stats.index_file_size, stats.shaders_file_size, stats.meta_file_size);

    std::filesystem::remove_all(test_dir);
    return 0;
}
