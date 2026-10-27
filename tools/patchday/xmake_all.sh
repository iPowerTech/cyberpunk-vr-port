#!/bin/sh
# Build the psiberx framework checkouts (ArchiveXL, TweakXL, Codeware) one after another via xmake_build.cmd.
#   sh tools/patchday/xmake_all.sh [checkout ...]     (default: all three)
# Logs: build/patchday/logs/<checkout>_build.log; prints each exit code and the distinct compile/link errors.
# The cmd paths are built with printf, not inside a double-quoted string: bash turns "\\$fw" into a literal "$fw".
ROOT=$(cd "$(dirname "$0")/../.." && pwd -W)
cd "$ROOT" || exit 1
[ $# -eq 0 ] && set -- cp2077-archive-xl cp2077-tweak-xl cp2077-codeware
for fw in "$@"; do
    src=$(printf '%s\\build\\patchday\\framework-src\\%s' "$(echo "$ROOT" | tr / '\\')" "$fw")
    log=$(printf '%s\\build\\patchday\\logs\\%s_build.log' "$(echo "$ROOT" | tr / '\\')" "$fw")
    MSYSTEM= cmd //c "tools\\patchday\\xmake_build.cmd $src $log" > /dev/null 2>&1
    echo "$fw EXIT $?"
    grep -E "error C|error:|error LNK" "$ROOT/build/patchday/logs/${fw}_build.log" 2>/dev/null \
        | sed 's/\x1b\[[0-9;]*m//g' | sort | uniq -c | sort -rn | head -15
done
