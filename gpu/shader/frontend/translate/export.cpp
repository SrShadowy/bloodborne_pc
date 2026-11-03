// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// EXP: render target exports (unpacked from 16-bit pairs when compressed, masked by the export
// format, swizzled to the color buffer's component order and converted), depth/stencil/sample
// mask exports, positions (with clip/cull distances) and parameters.
#include "shader/frontend/translate/translate.h"
#include "shader/ir/position.h"
#include "shader/ir/reinterpret.h"
#include "shader/profile.h"
#include "shader/runtime_info.h"
#include <atomic> // [extra] first-degrade log below
#include <cstdio> // [extra]

namespace Shader::Gcn {

namespace {

using ::Gcn::ShaderExportFormat;

::Gcn::NumberFormat CompressedNumberFormat(ShaderExportFormat format) {
    switch (format) {
    case ShaderExportFormat::ABGR_FP16:
        return ::Gcn::NumberFormat::Float;
    case ShaderExportFormat::ABGR_UNORM16:
        return ::Gcn::NumberFormat::Unorm;
    case ShaderExportFormat::ABGR_SNORM16:
        return ::Gcn::NumberFormat::Snorm;
    case ShaderExportFormat::ABGR_UINT16:
        return ::Gcn::NumberFormat::Uint;
    case ShaderExportFormat::ABGR_SINT16:
        return ::Gcn::NumberFormat::Sint;
    default:
        UNREACHABLE_MSG("Unimplemented compressed export format {}", static_cast<u32>(format));
    }
}

/// The components an uncompressed 32-bit export format keeps.
u32 ExportedComponents(u32 mask, ShaderExportFormat format) {
    switch (format) {
    case ShaderExportFormat::R_32:
        return mask & 1;
    case ShaderExportFormat::GR_32:
        return mask & 3;
    case ShaderExportFormat::AR_32:
        return mask & 9;
    case ShaderExportFormat::ABGR_32:
        return mask;
    default:
        UNREACHABLE_MSG("Unimplemented uncompressed export format {}", static_cast<u32>(format));
    }
}

bool NeedsUnormFixup(const Profile& profile, const auto& color_buffer) {
    using ::Gcn::DataFormat;
    using ::Gcn::NumberFormat;
    const auto nfmt = color_buffer.num_format;
    const auto dfmt = color_buffer.data_format;
    return profile.needs_unorm_fixup &&
           (nfmt == NumberFormat::Unorm || nfmt == NumberFormat::Snorm ||
            nfmt == NumberFormat::Srgb) &&
           (dfmt == DataFormat::Format8 || dfmt == DataFormat::Format8_8 ||
            dfmt == DataFormat::Format8_8_8_8);
}

} // namespace

// [extra] W0.2a (NextStep, debate 2026-11-03): hardware takes COMP as a no-op when the export
// format is a 32-bit one (Zero/R_32/GR_32/AR_32/ABGR_32) — compression only exists for the
// 16-bit pack formats below. Some Bloodborne shaders emit compr=1 with ABGR_32; they used to
// die in CompressedNumberFormat's UNREACHABLE. Now they take the uncompressed path, and the
// FIRST degrade logs the shader so a live offender becomes a capture for the golden-image
// test. Formats above ABGR_32 stay UNREACHABLE (defensive: never silently translated).
[[nodiscard]] bool IsCompressedExportFormat(ShaderExportFormat format) {
    return format >= ShaderExportFormat::ABGR_FP16 && format <= ShaderExportFormat::ABGR_SINT16;
}

/// The four components of an export (empty where not exported): from VGPR pairs of 16-bit
/// values (compressed: src0 = R, G; src1 = B, A) or one VGPR each.
static std::array<IR::F32, 4> ExportComponents(IR::IREmitter& ir, const GcnInst& inst,
                                               ShaderExportFormat format, u32 mask,
                                               bool alpha_in_compressed, u64 pgm_hash) {
    std::array<IR::F32, 4> components{};
    const auto& exp = inst.control.exp;
    if (!exp.compr || !IsCompressedExportFormat(format)) {
        if (exp.compr) { // [extra] degraded branch: compressed EXP with a 32-bit export format
            static std::atomic_flag logged = ATOMIC_FLAG_INIT;
            if (!logged.test_and_set()) {
                std::fprintf(stderr,
                             "[BBPort] compressed EXP with uncompressed export format %u "
                             "in shader %#llx: exporting raw VGPRs (hardware COMP no-op). "
                             "If this frame renders wrong, this shader is the W0.2 golden candidate.\n",
                             static_cast<u32>(format), (unsigned long long)pgm_hash);
                std::fflush(stderr);
            }
        }
        mask = ExportedComponents(mask, format);
        for (u32 i = 0; i < 4; i++, mask >>= 1) {
            if (mask & 1) {
                components[i] = ir.GetVectorReg<IR::F32>(IR::VectorReg(inst.src[i].code));
            }
        }
        return components;
    }
    const auto num_format = CompressedNumberFormat(format);
    if (exp.en & 1) {
        const IR::Value rg =
            ir.Unpack2x16(num_format, ir.GetVectorReg(IR::VectorReg(inst.src[0].code)));
        components[0] = IR::F32{ir.CompositeExtract(rg, 0)};
        components[1] = IR::F32{ir.CompositeExtract(rg, 1)};
    }
    if ((exp.en >> 2) & 1) {
        const IR::Value ba =
            ir.Unpack2x16(num_format, ir.GetVectorReg(IR::VectorReg(inst.src[1].code)));
        components[2] = IR::F32{ir.CompositeExtract(ba, 0)};
        if (alpha_in_compressed) {
            components[3] = IR::F32{ir.CompositeExtract(ba, 1)};
        }
    }
    return components;
}

void Translator::ExportRenderTarget(const GcnInst& inst) {
    const auto& exp = inst.control.exp;
    const IR::Attribute mrt{exp.target};
    info.mrt_mask |= 1u << static_cast<u8>(mrt);

    // With dual source blending MRT1 is the second source of color buffer 0.
    u32 cb_index = static_cast<u32>(mrt) - static_cast<u32>(IR::Attribute::RenderTarget0);
    if (runtime_info.hw.fs.dual_source_blending && mrt == IR::Attribute::RenderTarget1) {
        cb_index = 0;
    }
    const auto color_buffer = runtime_info.hw.fs.color_buffers[cb_index];
    if (color_buffer.export_format == ShaderExportFormat::Zero || exp.en == 0) {
        return;
    }
    const auto components =
        ExportComponents(ir, inst, color_buffer.export_format, exp.en, true, info.pgm_hash);
    // Hosts whose float to 8-bit unorm rounding is off get a bias.
    const bool unorm_fixup = NeedsUnormFixup(profile, color_buffer);
    for (u32 i = 0; i < 4; ++i) {
        const IR::F32 component = components[color_buffer.swizzle.Map(i)];
        if (component.IsEmpty()) {
            continue;
        }
        auto converted = ApplyWriteNumberConversion(ir, component, color_buffer.num_conversion);
        if (unorm_fixup) {
            converted = ir.FPSub(converted, ir.Imm32(1.f / 127500.f));
        }
        ir.SetAttribute(mrt, converted, i);
    }
}

void Translator::ExportDepth(const GcnInst& inst) {
    const auto& exp = inst.control.exp;
    if (exp.en == 0) {
        return;
    }
    const auto& fs = runtime_info.hw.fs;
    // (Compressed: no sample mask from the high half of the second pair.)
    const auto components =
        ExportComponents(ir, inst, fs.z_export_format, exp.en & fs.mrtz_mask, false,
                         info.pgm_hash);
    static constexpr std::array MrtzBuiltins = {IR::Attribute::Depth, IR::Attribute::StencilRef,
                                                IR::Attribute::SampleMask, IR::Attribute::Null};
    for (u32 i = 0; i < 4; ++i) {
        if (!components[i].IsEmpty()) {
            ir.SetAttribute(MrtzBuiltins[i], components[i]);
        }
    }
}

void Translator::EmitExport(const GcnInst& inst) {
    // "Valid mask": lanes outside EXEC are discarded.
    if (info.hw_stage == HwStage::Fragment && inst.control.exp.vm) {
        ir.Discard(ir.LogicalNot(ir.GetExec()));
    }
    const IR::Attribute attribute{inst.control.exp.target};
    if (IR::IsMrt(attribute)) {
        return ExportRenderTarget(inst);
    }
    if (attribute == IR::Attribute::Depth) {
        return ExportDepth(inst);
    }
    ASSERT_MSG(!inst.control.exp.compr, "Compressed exports only supported for render targets");
    const bool tess_emulated_primitive =
        info.sw_stage == SwStage::Vertex && runtime_info.sw.vs.tess_emulated_primitive;
    u32 mask = inst.control.exp.en;
    for (u32 i = 0; i < 4; i++, mask >>= 1) {
        if ((mask & 1) == 0) {
            continue;
        }
        const auto value = ir.GetVectorReg<IR::F32>(IR::VectorReg(inst.src[i].code));
        if (IsPosition(attribute)) {
            IR::ExportPosition(ir, runtime_info.hw.vs, tess_emulated_primitive, attribute, i,
                               value);
        } else {
            ir.SetAttribute(attribute, value, i);
        }
    }
}

} // namespace Shader::Gcn
