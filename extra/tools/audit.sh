#!/usr/bin/env bash
# extra/tools/audit.sh — quick governance audit for the [extra] boundary (AGENTS.md section 2):
#   1. extra/ file sizes (target <= 300 lines, hard limit <= 400), ratcheted by the embedded
#      list below: known debt may stay or shrink only — a NEW file over 400 lines, or a
#      baselined file that GREW, is a hard FAIL (the exit code gates again).
#   2. upstream files must include "extra/hooks.h" at most once
#   3. report the upstream hook-marker count (informational)
# Exit code 1 = hard violation. Run after any change, or via ./extra/tools/qa_runner.sh.
# Refresh the embedded list after paying down debt (paste the output over the block below):
#   find extra -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
#       | xargs wc -l | awk '$1 > 300 && $2 != "total" {print $1, $2}'
set -uo pipefail
cd -- "$(dirname -- "$0")/../.."

fail=0

# Recorded size debt (<lines> <path>). Embedded in this script on purpose: extra/tools/ is not
# versioned and a side file (audit_baseline.txt) here vanished twice; the debt list can only
# change by a deliberate edit anyway — the same review that pays it down updates this block.
# Empty: every extra/ file is at or below the 300-line target.
baseline_debt=''

echo "== [1] extra/ file sizes (target <= 300, hard limit <= 400, ratchet)"
while IFS= read -r f; do
    lines=$(wc -l < "$f")
    if (( lines > 300 )); then
        base=$(printf '%s\n' "$baseline_debt" | awk -v p="$f" '$2 == p {print $1; exit}')
        if [[ -n $base ]]; then
            if (( lines > base )); then
                echo "FAIL: $f grew past its recorded debt ($base -> $lines lines)"
                fail=1
            elif (( lines < base )); then
                echo "DEBT: $f has $lines lines (baseline $base - shrank, lower the baseline)"
            else
                echo "DEBT: $f has $lines lines (baseline $base)"
            fi
        elif (( lines > 400 )); then
            echo "FAIL: $f has $lines lines (hard limit 400, not in baseline)"
            fail=1
        else
            echo "WARN: $f has $lines lines (target 300)"
        fi
    fi
done < <(find extra -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \))

echo "== [2] upstream files including extra/hooks.h more than once"
while IFS= read -r f; do
    n=$(grep -c 'extra/hooks\.h' "$f" || true)
    if (( n > 1 )); then
        echo "FAIL: $f includes extra/hooks.h $n times (max 1)"
        fail=1
    fi
done < <(grep -rl 'extra/hooks\.h' --include='*.cpp' --include='*.h' . | grep -v '^\./extra/' || true)

echo "== [3] upstream // [extra] hook markers (informational)"
grep -rn '\[extra\]' --include='*.cpp' --include='*.h' --include='*.txt' . \
    | grep -v '^\./extra/' | grep -v third_party | wc -l

exit $fail
