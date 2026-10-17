#!/usr/bin/env bash
# The standard verify of a change: the four builds, then the 32-bit OpenGL
# and the 64-bit Vulkan suites, one line per step.
#
#   pc/tools/verify.sh                   everything
#   pc/tools/verify.sh --changed [BASE]  the suites a change since BASE (default
#                                        main) can affect (run.py --changed)
#   pc/tools/verify.sh --no-windows      skip the Windows cross build
#   pc/tools/verify.sh -- ARGS...        more run.py options for both suites
#                                        (e.g. -- -j 4 --gpu-jobs 3)
#
# Each step holds $ROOD_HEAVY_LOCK (flock) while it runs, when set, so
# parallel sessions interleave step by step instead of overlapping, and
# starts only once the CPU package is below 95 C (else waits for 85 C). Logs:
# pc/build/verify/<step>.log; suite outputs in pc/build/verify/<suite run>/.
# Exit status: the number of failed steps (0 = all passed).
set -u
cd "$(dirname "$0")/../.."
changed=() windows=1 extra=()
while [ $# -gt 0 ]; do
    case "$1" in
        --changed)
            if [ $# -gt 1 ] && [ "${2#-}" = "$2" ]; then changed=(--changed "$2"); shift; else changed=(--changed); fi ;;
        --no-windows) windows=0 ;;
        --) shift; extra=("$@"); break ;;
        -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "verify.sh: unknown option $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
out=pc/build/verify
mkdir -p "$out"
failed=0 start_all=$SECONDS

step() {  # step NAME COMMAND...: run it (under the lock), print one line
    local name=$1; shift
    local log="$out/$name.log" t=$SECONDS status
    python3 pc/tests/run.py --wait-cool  # a hot CPU package first cools (run.py --thermal-pause)
    if [ -n "${ROOD_HEAVY_LOCK:-}" ]; then
        flock "$ROOD_HEAVY_LOCK" "$@" > "$log" 2>&1
    else
        "$@" > "$log" 2>&1
    fi
    status=$?
    local summary
    summary=$(grep -E '^[0-9]+/[0-9]+ passed|^nothing to run|^--changed: |^Built ' "$log" | tail -1 \
        | sed -E 's|^Built [^:]*/([^/:]+):.*|built \1|')
    summary=${summary:-$(tail -1 "$log" | cut -c1-100)}
    if [ $status -eq 0 ]; then
        printf 'PASS  %-12s %5ds  %s\n' "$name" $((SECONDS - t)) "$summary"
    else
        failed=$((failed + 1))
        printf 'FAIL  %-12s %5ds  %s (%s)\n' "$name" $((SECONDS - t)) "$summary" "$log"
    fi
}

step build-32 python3 pc/tools/build_boot.py
step battle-32 python3 pc/tools/build_boot.py --battle
step build-64 python3 pc/tools/build_boot.py --arch 64
[ $windows = 1 ] && step windows python3 pc/tools/build_boot.py --target windows
step suite-32gl python3 pc/tests/run.py --out "$out/t32gl" "${changed[@]}" "${extra[@]}"
step suite-64vk python3 pc/tests/run.py --arch 64 --renderer vulkan --out "$out/t64vk" "${changed[@]}" "${extra[@]}"
printf '%s in %ds\n' "$([ $failed = 0 ] && echo 'all passed' || echo "$failed failed")" $((SECONDS - start_all))
exit $failed
