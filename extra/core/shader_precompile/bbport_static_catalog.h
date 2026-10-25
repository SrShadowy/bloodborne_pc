// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace BbStaticCatalog {

enum class ShaderClassification {
    ConfirmedMatch, // Present in static catalog and drawn at runtime
    StaticOnly,     // Present in static catalog, not yet drawn
    RuntimeOnly,    // Drawn/compiled at runtime, not in static catalog (fetch shaders, compute copy, etc.)
    StaleRejected   // Damaged, permutation mismatch, or discarded
};

struct ClassificationStats {
    uint32_t confirmed_match = 0;
    uint32_t static_only = 0;
    uint32_t runtime_only = 0;
    uint32_t total_static = 0;
    uint32_t total_runtime = 0;
};

struct StaticShaderInfo {
    uint64_t hash = 0;
    uint32_t crc32 = 0;
    std::string stage;
    std::vector<std::string> bundles;
    uint32_t code_size = 0;
    std::string sha256;
};

struct BundleStat {
    std::string name;
    uint32_t count = 0;
    uint64_t decompressed_size = 0;
    uint64_t packed_size = 0;
};

bool LoadCatalog(const std::filesystem::path& catalog_path);
bool AutoLoad(const std::filesystem::path& cache_dir);
bool IsLoaded();

uint32_t GetTotalStaticShaders();
uint32_t GetTotalStaticInstances();
std::vector<BundleStat> GetBundleStats();

bool FindStaticShader(uint64_t hash, StaticShaderInfo& out_info);
bool IsKnownStatic(uint64_t hash);

void RecordRuntimeShader(uint64_t hash);
uint32_t GetDiscoveredStaticCount();
float GetStaticCoveragePercentage();

// Phase 2 Classification & Diagnostics
ClassificationStats GetClassificationStats();
ShaderClassification ClassifyShader(uint64_t hash);
const char* ClassificationName(ShaderClassification c);
bool ExportDiagnosticReport(const std::filesystem::path& output_path);

} // namespace BbStaticCatalog
