// SPDX-License-Identifier: GPL-2.0-or-later
// bbport render: compute shaders done by the renderer instead of being translated. Only the
// game's buffer copy shader (from its profile), when the game's memory is not in place.
#pragma once

namespace Gcn {
struct ComputeProgram;
union Regs;
} // namespace Gcn

namespace Shader {
struct Info;
}

namespace Vulkan {

class Rasterizer;

/// True if the dispatch was done here.
bool ExecuteShaderHLE(const Shader::Info& info, const Gcn::Regs& regs,
                      const Gcn::ComputeProgram& cs_program, Rasterizer& rasterizer);

} // namespace Vulkan
