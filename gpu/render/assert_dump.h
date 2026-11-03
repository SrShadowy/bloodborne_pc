// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// [extra] F0a (NextStep, DEBATE #3): BB_ASSERT_DUMP=<dir> turns a failed shader check into a
// NAMED ARTIFACT, <dir>/<site>/<hash>-<perm>.bin, instead of only an [AssertLog] line. Nothing
// new is instrumented: the bytes are a standard BbShaderCapture input record (WriteInput), so
// shader-corpus-test replays every offender as it is written. `site` is the failed check's
// text ("is_flat", "IsSharpSource(...)"), so asserts/ IS the site x offender coverage the W0
// matrix (F0b) compares the .bbsc corpus against. Off: no state, no copies, no cost.
#pragma once

#include "common/types.h"

#include <span>
#include <vector>

namespace BbAssertDump {

/// Whether artifacts are written (BB_ASSERT_DUMP set to a directory).
bool Enabled();

/// Writes one artifact: the input record of the compilation whose check failed (`bytes` from
/// BbShaderCapture::WriteInput/TakeInput), under <dir>/<sanitized `site`>/ named by the shader
/// hash and permutation. One artifact per (site, hash, perm): an existing name is kept.
void Write(const char* site, u64 pgm_hash, u64 perm_idx, std::span<const u8> bytes);

} // namespace BbAssertDump
