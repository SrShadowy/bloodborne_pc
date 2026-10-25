// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_static_catalog.h"
#include "bbport_shader_discovery.h"
#include "bbport_vk_pipeline_cache.h"

#include <chrono>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>

#include "common/logging/log.h"

namespace BbStaticCatalog {

namespace {

std::mutex s_mutex;
bool s_loaded = false;
uint32_t s_total_instances = 0;
std::unordered_map<uint64_t, StaticShaderInfo> s_shaders;
std::vector<BundleStat> s_bundles;
std::unordered_set<uint64_t> s_discovered_static;

uint64_t ParseHexU64(const std::string& str) {
    try {
        return std::stoull(str, nullptr, 16);
    } catch (...) {
        return 0;
    }
}

uint32_t ParseHexU32(const std::string& str) {
    try {
        return static_cast<uint32_t>(std::stoul(str, nullptr, 16));
    } catch (...) {
        return 0;
    }
}

} // namespace

bool LoadCatalog(const std::filesystem::path& catalog_path) {
    std::lock_guard lock(s_mutex);
    if (!std::filesystem::exists(catalog_path)) {
        return false;
    }

    try {
        std::ifstream ifs(catalog_path);
        if (!ifs.is_open()) return false;

        nlohmann::json j;
        ifs >> j;

        s_shaders.clear();
        s_bundles.clear();
        s_discovered_static.clear();

        s_total_instances = j.value("total_instances", 0u);

        if (j.contains("bundle_summaries") && j["bundle_summaries"].is_object()) {
            for (auto& [bname, binfo] : j["bundle_summaries"].items()) {
                BundleStat b{};
                b.name = bname;
                b.count = binfo.value("count", 0u);
                b.decompressed_size = binfo.value("decompressed_size", 0ULL);
                b.packed_size = binfo.value("packed_size", 0ULL);
                s_bundles.push_back(std::move(b));
            }
        }

        if (j.contains("shaders") && j["shaders"].is_object()) {
            for (auto& [k, v] : j["shaders"].items()) {
                StaticShaderInfo info{};
                info.hash = ParseHexU64(k);
                info.crc32 = ParseHexU32(v.value("crc32", "0x0"));
                info.stage = v.value("stage", "Unknown");
                info.code_size = v.value("code_size", 0u);
                info.sha256 = v.value("sha256", "");

                if (v.contains("source_bundles") && v["source_bundles"].is_array()) {
                    for (const auto& b : v["source_bundles"]) {
                        info.bundles.push_back(b.get<std::string>());
                    }
                }

                if (info.hash != 0) {
                    s_shaders[info.hash] = std::move(info);
                }
            }
        }

        s_loaded = true;
        LOG_INFO(Render, "StaticShaderCatalog: loaded {} unique shaders ({} total instances) from {}",
                 s_shaders.size(), s_total_instances, catalog_path.string());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(Render, "StaticShaderCatalog: failed to parse {}: {}", catalog_path.string(), e.what());
        return false;
    }
}

bool AutoLoad(const std::filesystem::path& cache_dir) {
    if (IsLoaded()) return true;

    const std::vector<std::filesystem::path> candidates = {
        cache_dir / "static_shader_catalog.json",
        cache_dir / "CUSA03173" / "static_shader_catalog.json",
        std::filesystem::path("dist/user/cache/CUSA03173/static_shader_catalog.json"),
        std::filesystem::path("user/cache/CUSA03173/static_shader_catalog.json"),
    };

    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            if (LoadCatalog(p)) return true;
        }
    }
    return false;
}

bool IsLoaded() {
    std::lock_guard lock(s_mutex);
    return s_loaded;
}

uint32_t GetTotalStaticShaders() {
    std::lock_guard lock(s_mutex);
    return static_cast<uint32_t>(s_shaders.size());
}

uint32_t GetTotalStaticInstances() {
    std::lock_guard lock(s_mutex);
    return s_total_instances;
}

std::vector<BundleStat> GetBundleStats() {
    std::lock_guard lock(s_mutex);
    return s_bundles;
}

bool FindStaticShader(uint64_t hash, StaticShaderInfo& out_info) {
    std::lock_guard lock(s_mutex);
    auto it = s_shaders.find(hash);
    if (it != s_shaders.end()) {
        out_info = it->second;
        return true;
    }
    return false;
}

bool IsKnownStatic(uint64_t hash) {
    std::lock_guard lock(s_mutex);
    return s_shaders.find(hash) != s_shaders.end();
}

void RecordRuntimeShader(uint64_t hash) {
    std::lock_guard lock(s_mutex);
    if (s_shaders.find(hash) != s_shaders.end()) {
        s_discovered_static.insert(hash);
    }
}

uint32_t GetDiscoveredStaticCount() {
    std::lock_guard lock(s_mutex);
    return static_cast<uint32_t>(s_discovered_static.size());
}

float GetStaticCoveragePercentage() {
    std::lock_guard lock(s_mutex);
    if (s_shaders.empty()) return 0.0f;
    return (static_cast<float>(s_discovered_static.size()) / static_cast<float>(s_shaders.size())) * 100.0f;
}

ShaderClassification ClassifyShader(uint64_t hash) {
    std::lock_guard lock(s_mutex);
    if (s_shaders.find(hash) != s_shaders.end()) {
        return (s_discovered_static.find(hash) != s_discovered_static.end())
            ? ShaderClassification::ConfirmedMatch
            : ShaderClassification::StaticOnly;
    }
    return ShaderClassification::RuntimeOnly;
}

const char* ClassificationName(ShaderClassification c) {
    switch (c) {
    case ShaderClassification::ConfirmedMatch: return "MATCH";
    case ShaderClassification::StaticOnly:     return "STATIC";
    case ShaderClassification::RuntimeOnly:    return "RUNTIME";
    case ShaderClassification::StaleRejected:  return "REJECTED";
    }
    return "UNKNOWN";
}

ClassificationStats GetClassificationStats() {
    ClassificationStats stats{};
    std::lock_guard lock(s_mutex);
    stats.total_static = static_cast<uint32_t>(s_shaders.size());
    stats.confirmed_match = static_cast<uint32_t>(s_discovered_static.size());
    stats.static_only = (stats.total_static > stats.confirmed_match)
        ? (stats.total_static - stats.confirmed_match) : 0;

    const auto rt_shaders = BbShaderDiscovery::GetShaders();
    stats.total_runtime = static_cast<uint32_t>(rt_shaders.size());
    for (const auto& sh : rt_shaders) {
        if (s_shaders.find(sh.pgm_hash) == s_shaders.end()) {
            stats.runtime_only++;
        }
    }
    return stats;
}

bool ExportDiagnosticReport(const std::filesystem::path& output_path) {
    try {
        nlohmann::json j;
        const auto driver = BbVkPipelineCache::GetDriverCacheStatus();
        j["hardware"]["vendor_id"] = driver.vendor_id;
        j["hardware"]["device_id"] = driver.device_id;
        j["hardware"]["device_name"] = driver.device_name;
        j["driver_cache"]["loaded"] = driver.loaded;
        j["driver_cache"]["size_bytes"] = driver.size_bytes;
        j["driver_cache"]["cold_forced"] = driver.cold_forced;

        const auto cstats = GetClassificationStats();
        j["classification"]["confirmed_match"] = cstats.confirmed_match;
        j["classification"]["static_only"] = cstats.static_only;
        j["classification"]["runtime_only"] = cstats.runtime_only;
        j["classification"]["total_static"] = cstats.total_static;
        j["classification"]["total_runtime"] = cstats.total_runtime;
        j["classification"]["coverage_pct"] = GetStaticCoveragePercentage();

        uint32_t preloaded = 0, total_keys = 0;
        BbShaderDiscovery::GetProgress(preloaded, total_keys);
        j["pipelines"]["reconstructed"] = preloaded;
        j["pipelines"]["total_keys"] = total_keys;
        j["pipelines"]["stale_rejected"] = (total_keys > preloaded) ? (total_keys - preloaded) : 0;
        j["pipelines"]["success_pct"] = total_keys > 0 ? (100.0 * preloaded / total_keys) : 0.0;

        const auto dstats = BbShaderDiscovery::GetStats();
        j["telemetry"]["unique_shaders"] = dstats.unique_shaders;
        j["telemetry"]["unique_pipelines"] = dstats.unique_pipelines;
        j["telemetry"]["shader_hits"] = dstats.shader_hits;
        j["telemetry"]["pipeline_hits"] = dstats.pipeline_hits;

        std::filesystem::create_directories(output_path.parent_path());
        std::ofstream ofs(output_path);
        if (!ofs.is_open()) return false;
        ofs << j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace BbStaticCatalog
