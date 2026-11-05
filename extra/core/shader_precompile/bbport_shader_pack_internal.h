// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the shader-archive files share. bbport_shader_pack.cpp owns the paths and the
// small helpers, bbport_shader_pack_inspect.cpp answers questions about an archive,
// bbport_shader_pack_pack.cpp builds one, bbport_shader_pack_unpack.cpp expands one and
// bbport_shader_pack_restore.cpp seeds an empty cache from a shipped one. Only this module needs
// this header.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "bbport_shader_pack.h"

namespace BbShaderPack::detail {

/// The archive's three files in one cache directory.
struct ArchivePaths {
    std::filesystem::path index;
    std::filesystem::path shaders;
    std::filesystem::path metadata;
};

/// Where the archive's files live: `cache_dir` joined with the three archive names.
ArchivePaths PathsIn(const std::filesystem::path& cache_dir);

/// The game serial the cache belongs to, or "CUSA03173" when the ELF is not loaded yet.
std::string GameSerialOrDefault();

/// `dir` when given, else the game serial's cache directory.
std::filesystem::path ResolveCacheDir(const std::filesystem::path& dir);

/// The install layouts a shipped master cache may sit in, in the order they are tried.
std::vector<std::filesystem::path> MasterCacheCandidates(const std::string& serial);

/// Reads a whole file; false when it cannot be opened or is empty.
bool ReadFileBytes(const std::filesystem::path& path, std::vector<std::uint8_t>& out);

} // namespace BbShaderPack::detail
