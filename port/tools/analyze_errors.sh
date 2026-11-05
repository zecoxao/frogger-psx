#!/bin/bash
# Summarize the keep-going build log by error category.
LOG="${1:-/tmp/build.log}"
echo "=== totals ==="
echo "error lines : $(grep -c 'error:' "$LOG")"
echo "lvalue-cast : $(grep -c 'lvalue required' "$LOG")"
echo "gte_ macros : $(grep -c 'implicit declaration of function .gte_' "$LOG")"
echo "other impl  : $(grep 'implicit declaration' "$LOG" | grep -vc 'gte_')"
echo
echo "=== distinct non-gte implicit functions ==="
grep 'implicit declaration' "$LOG" | grep -v 'gte_' \
  | grep -oE "function ‘[A-Za-z_][A-Za-z0-9_]+" | sed 's/function ‘//' \
  | sort | uniq -c | sort -rn | head -n 30
echo
echo "=== other (non-lvalue, non-implicit) error messages ==="
grep 'error:' "$LOG" | grep -vE 'lvalue required|implicit declaration' \
  | sed -E 's/^.*error: //; s/[0-9]+/N/g' | sort | uniq -c | sort -rn | head -n 20
echo
echo "=== failing files ==="
grep 'FAILED:' "$LOG" | grep -oE '[A-Za-z0-9_]+\.[cC]\.o' | sort -u
