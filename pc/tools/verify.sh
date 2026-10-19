#!/usr/bin/env bash
# The standard verify of a change: the four builds, then the 32-bit OpenGL
# and the 64-bit Vulkan suites, one line per step.
#
#   pc/tools/verify.sh                   everything
#   pc/tools/verify.sh --changed [BASE]  the suites a change since BASE (default
#                                        main) can affect (run.py --changed)
#   pc/tools/verify.sh --no-windows      skip the Windows cross build
#   pc/tools/verify.sh --windows         include it with --changed (skipped
#                                        there by default: low priority)
#   pc/tools/verify.sh -- ARGS...        more run.py options for both suites
#                                        (e.g. -- -j 4 --gpu-jobs 3)
#
# Builds hold $ROOD_BUILD_LOCK and test suites $ROOD_HEAVY_LOCK (flock;
# without a build lock both take the heavy one), so one session's build runs
# beside another's tests while the GPU suites never overlap. A step starts
# once the CPU package is below run.py's thermal pause. The Windows build
# comes last, at nice 19 (low priority). Logs:
# pc/build/verify/<step>.log; suite outputs in pc/build/verify/<suite run>/.
# Exit status: the number of failed steps (0 = all passed).
set -u
cd "$(dirname "$0")/../.."
changed=() windows= extra=()
while [ $# -gt 0 ]; do
    case "$1" in
        --changed)
            if [ $# -gt 1 ] && [ "${2#-}" = "$2" ]; then changed=(--changed "$2"); shift; else changed=(--changed); fi ;;
        --no-windows) windows=0 ;;
        --windows) windows=1 ;;
        --) shift; extra=("$@"); break ;;
        -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "verify.sh: unknown option $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
# Windows: on for a full verify, off for --changed unless asked (low priority).
[ -z "$windows" ] && { [ ${#changed[@]} -gt 0 ] && windows=0 || windows=1; }
out=pc/build/verify
mkdir -p "$out"
failed=0 start_all=$SECONDS

step() {  # step build|test NAME COMMAND...: run it (under its lock), print one line
    local kind=$1 name=$2; shift 2
    local log="$out/$name.log" t=$SECONDS status
    local lock=${ROOD_HEAVY_LOCK:-}
    [ "$kind" = build ] && [ -n "${ROOD_BUILD_LOCK:-}" ] && lock=$ROOD_BUILD_LOCK
    python3 pc/tests/run.py --wait-cool  # a hot CPU package first cools (run.py --thermal-pause)
    if [ -n "$lock" ]; then
        flock "$lock" "$@" > "$log" 2>&1
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

step build build-32 python3 pc/tools/build_boot.py
step build battle-32 python3 pc/tools/build_boot.py --battle
step build build-64 python3 pc/tools/build_boot.py --arch 64
step test suite-32gl python3 pc/tests/run.py --out "$out/t32gl" "${changed[@]}" "${extra[@]}"
step test suite-64vk python3 pc/tests/run.py --arch 64 --renderer vulkan --out "$out/t64vk" "${changed[@]}" "${extra[@]}"
[ $windows = 1 ] && step build windows nice -n 19 python3 pc/tools/build_boot.py --target windows
printf '%s in %ds\n' "$([ $failed = 0 ] && echo 'all passed' || echo "$failed failed")" $((SECONDS - start_all))
exit $failed
