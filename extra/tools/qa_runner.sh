#!/usr/bin/env bash
# extra/tools/qa_runner.sh — the full pre-delivery QA gate (AGENTS.md section 2).
#   [1] audit.sh           extra/ size ratchet (embedded baseline) + hook-marker check
#   [2] build              bash build.sh --no-bump + ninja pipeline-reconstruct-test, on the
#                          HOST: the game's run.sh builds on the host at every launch, so a
#                          docker build here would keep fighting a manifest the next host
#                          launch rewrites anyway. Mirrors run.sh's nix-shell re-exec when the
#                          host shell lacks the toolchain.
#   [3] runtime smoke      offline pipeline reconstruction in a throwaway sandbox ($BB_QA_DIR,
#                          default /tmp/opencode/qa_sandbox): exit 0, creation-feedback self-test
#                          OK, and the driver-cache blob round-trips byte-identical across two
#                          warm runs (the F0 persistence net). A third run with BB_PIPELINE_STATS=1
#                          checks the armed path ("feedback armed" is driver-dependent, so it is
#                          informational). Visual frame validation needs the game binary and stays
#                          manual.
# Prints PASS/SKIP/FAIL per stage and exits 1 if anything failed. A full pass takes minutes.
set -uo pipefail
cd -- "$(dirname -- "$0")/../.."

fails=0
pass()   { echo "PASS: $*"; }
skip()   { echo "SKIP: $*"; }
failst() { echo "FAIL: $*"; fails=$((fails+1)); }

echo "== [1/3] audit.sh"
if ./extra/tools/audit.sh; then pass audit; else failst audit; fi

echo "== [2/3] build (bbgpu + pipeline-reconstruct-test, host)"
b_log=/tmp/opencode/qa_build.log
build_failed=0
build_all() { bash build.sh --no-bump && ninja -C out/gpu pipeline-reconstruct-test; }
# run.sh re-execs through nix-shell when the host shell lacks the toolchain; same path here.
if ! { command -v pkg-config >/dev/null && pkg-config --exists vulkan sdl3; } \
        && command -v nix-shell >/dev/null; then
    build_all() {
        env BB_IN_NIX_SHELL=1 nix-shell shell.nix \
            --run 'bash build.sh --no-bump && ninja -C out/gpu pipeline-reconstruct-test'
    }
fi
if build_all >"$b_log" 2>&1; then
    pass build
else
    failst "build - tail of $b_log:"
    tail -n 20 "$b_log"
    build_failed=1
fi

echo "== [3/3] runtime smoke (warm sandbox round-trip)"
bin=out/gpu/pipeline-reconstruct-test
libdir=$(ls -d dist/BBPort-*-linux-*/lib 2>/dev/null | head -1)
qa=${BB_QA_DIR:-/tmp/opencode/qa_sandbox}
blob=$qa/cache/CUSA03173/pipeline_driver.cache
smoke() { # $1 = log suffix, $2 = optional NAME=value extra env
    local ld="$PWD/$libdir:$PWD/out/gpu"
    if [[ -n ${2:-} ]]; then
        env "$2" LD_LIBRARY_PATH="$ld" "$bin" "$qa" --warm >"$qa/smoke_$1.log" 2>&1
    else
        env LD_LIBRARY_PATH="$ld" "$bin" "$qa" --warm >"$qa/smoke_$1.log" 2>&1
    fi
}
selftest_ok() { grep -q "Pipeline Feedback Self-Test: accounting=OK pNext-chaining=OK" "$qa/smoke_$1.log"; }
blob_md5() { md5sum "$blob" 2>/dev/null | cut -d' ' -f1; }
mkdir -p "$qa"
if (( build_failed )); then
    skip "stage 2 failed - running the stale binary would prove nothing"
elif [[ ! -x $bin ]]; then
    skip "test binary missing (stage 2 skipped or never built)"
elif [[ -z $libdir ]]; then
    skip "dist/BBPort-*-linux-*/lib not found"
else
    ok=1
    smoke run1 || { failst "smoke run1 exited $?"; ok=0; }
    h1=$(blob_md5)
    smoke run2 || { failst "smoke run2 exited $?"; ok=0; }
    h2=$(blob_md5)
    if (( ! ok )); then
        echo "note: smoke logs in $qa"
    elif ! selftest_ok run2; then
        failst "creation-feedback self-test - tail of $qa/smoke_run2.log:"
        tail -n 15 "$qa/smoke_run2.log"
    elif [[ -n $h1 && -n $h2 && $h1 != "$h2" ]]; then
        failst "driver blob round-trip ($h1 -> $h2)"
    elif [[ -z $h2 ]]; then
        echo "note: driver produced no blob; round-trip skipped"
        pass "smoke exit + self-test"
    else
        pass "smoke exit + self-test + blob round-trip ($h2)"
    fi
    if smoke armed BB_PIPELINE_STATS=1 && selftest_ok armed; then
        if grep -q "\[feedback armed\]" "$qa/smoke_armed.log"; then
            pass "armed smoke (extension enabled by the driver)"
        else
            pass "armed smoke (self-test OK; driver offers no feedback extension)"
        fi
    else
        failst "armed smoke - tail of $qa/smoke_armed.log:"
        tail -n 15 "$qa/smoke_armed.log"
    fi
fi

echo "== qa summary: $fails stage failure(s)"
exit $(( fails > 0 ))
