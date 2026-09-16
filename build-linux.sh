#!/usr/bin/env bash
#
# Double-clickable / terminal entry point for the Linux build, mirroring
# build-windows.bat: asks Release or Debug (unless passed explicitly) and
# builds. No menus, no server patching - see docs/building-linux.md for that.
#
# Pass --docker for a portable build (runs on any Linux, not just this
# machine) via the Rocky Linux 8 Docker image - see scripts/build-linux.sh
# --help for details. Without it, builds natively against this machine's
# system libraries (fast, but not portable to other distros).
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

CONFIGURATION=""
USE_DOCKER=0
for arg in "$@"; do
    case "$arg" in
        --debug) CONFIGURATION="Debug" ;;
        --release) CONFIGURATION="Release" ;;
        --docker) USE_DOCKER=1 ;;
    esac
done

if [ -z "$CONFIGURATION" ]; then
    echo "OwpenGram Desktop build"
    echo "Repo: $ROOT"
    echo ""
    while [ -z "$CONFIGURATION" ]; do
        read -r -p "Build type: [R]elease or [D]ebug [R]: " answer
        case "$(echo "$answer" | tr '[:upper:]' '[:lower:]')" in
            ""|r|release) CONFIGURATION="Release" ;;
            d|debug) CONFIGURATION="Debug" ;;
            *) echo "  Please enter R or D." ;;
        esac
    done
fi

# OWPENGRAM_BUILD is the release number this build ships under -- the N in
# tag "ON", compared against GitHub release tags by the client's own updater
# (see docs/updates.md) and shown in the About box next to the version. Skip
# the prompt when it's already set in the environment, so
# `OWPENGRAM_BUILD=8 ./build-linux.sh` still works untouched for scripted use.
if [ -z "${OWPENGRAM_BUILD:-}" ]; then
    while :; do
        read -r -p "Release number for this build (the N in tag ON; blank = local dev build, no self-update) [0]: " answer
        answer="${answer:-0}"
        if [[ "$answer" =~ ^[0-9]+$ ]]; then
            OWPENGRAM_BUILD="$answer"
            break
        fi
        echo "  Enter a whole number, or leave blank for a local dev build."
    done
fi
export OWPENGRAM_BUILD
if [ "$OWPENGRAM_BUILD" = "0" ]; then
    echo "Release number: 0 (local dev build - self-update stays off)"
else
    echo "Release number: $OWPENGRAM_BUILD (must match tag O$OWPENGRAM_BUILD once this is published)"
fi

ARGS=("--$(echo "$CONFIGURATION" | tr '[:upper:]' '[:lower:]')")
if [ "$USE_DOCKER" -eq 1 ]; then ARGS+=(--docker); fi

exec "$ROOT/scripts/build-linux.sh" "${ARGS[@]}"
