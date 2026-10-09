#!/usr/bin/env bash
#
# run.sh — every host-side test for the firmware. No board needed.
#
#   ./run.sh            build + run all suites (C++ via g++, web checks via node)
#   ./run.sh --build    …and also compile the real firmware with PlatformIO (both envs)
#   ./run.sh app ntp    only the named C++ suites (test_<name>.cpp)
#
# Each test_<name>.cpp is compiled against the REAL src/ headers (test_app.cpp compiles
# src/main.cpp itself) and the stand-ins in stub/. See README.md in this folder.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW="$(cd "$HERE/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/ubersdr-switch-tests"
mkdir -p "$OUT"
CXX="${CXX:-g++}"

BUILD=0; ONLY=()
for a in "$@"; do
    case "$a" in
        --build) BUILD=1 ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) ONLY+=("$a") ;;
    esac
done

pass=0; fail=0; failed=()
run() {   # name, command...
    local name="$1"; shift
    printf '\n\033[1m== %s ==\033[0m\n' "$name"
    if "$@"; then pass=$((pass + 1)); else fail=$((fail + 1)); failed+=("$name"); fi
}

for src in "$HERE"/test_*.cpp; do
    name="$(basename "$src" .cpp)"; name="${name#test_}"
    if [ ${#ONLY[@]} -gt 0 ] && [[ ! " ${ONLY[*]} " =~ " $name " ]]; then continue; fi
    bin="$OUT/test_$name"
    # The stand-ins in stub/ must win over any real ESP8266 headers.
    if ! "$CXX" -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter \
            -I "$HERE/stub" -I "$FW/src" -I "$HERE" -DDATA_DIR="\"$FW/data\"" \
            -o "$bin" "$src" "$HERE/stub/fakes.cpp" 2> "$OUT/test_$name.buildlog"; then
        printf '\n\033[1m== %s ==\033[0m\n   \033[31mdid not build\033[0m — %s\n' "$name" "$OUT/test_$name.buildlog"
        head -20 "$OUT/test_$name.buildlog" | sed 's/^/     /'
        fail=$((fail + 1)); failed+=("$name (build)"); continue
    fi
    # Firmware log lines go to stderr; keep them out of the report.
    run "$name" sh -c "'$bin' 2>/dev/null"
done

if [ ${#ONLY[@]} -eq 0 ]; then
    if command -v node >/dev/null 2>&1; then
        run web node "$HERE/test_web.mjs"
    else
        printf '\n\033[33mweb: skipped — node not installed\033[0m\n'
    fi
    run scripts sh -c "for f in '$FW'/*.sh '$FW'/../scripts/*.sh; do bash -n \"\$f\" || exit 1; done && echo '   ok    shell scripts parse (bash -n)'"
fi

if [ "$BUILD" = 1 ]; then
    PIO="$(command -v pio || command -v platformio || echo "$HOME/.platformio/penv/bin/pio")"
    run "firmware build" bash -c "cd '$FW' && '$PIO' run -e esp12e -e esp12e-debug > '$OUT/pio.log' 2>&1; rc=\$?; grep -E 'RAM:|Flash:|SUCCESS|FAILED|error' '$OUT/pio.log'; exit \$rc"
fi

printf '\n\033[1m%d suite(s) passed, %d failed\033[0m' "$pass" "$fail"
[ "$fail" -gt 0 ] && printf ' — %s' "${failed[*]}"
printf '\n'
[ "$fail" -eq 0 ]
