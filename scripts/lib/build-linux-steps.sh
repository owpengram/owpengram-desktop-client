#!/usr/bin/env bash
#
# Shared build steps for scripts/build-linux.sh and
# scripts/interactive-build-linux.sh. Not meant to be run directly - source it.
#
# Expects REPO_ROOT to already be set by the caller.

PACMAN_PACKAGES=(
    base-devel cmake ninja python git gperf mold
    qt6-base qt6-declarative qt6-svg qt6-wayland qt6-imageformats qt6-shadertools
    ffmpeg openal boost protobuf hunspell minizip lz4 xxhash range-v3
    tl-expected microsoft-gsl cmark-gfm kcoreaddons kimageformats fcitx5-qt
    libtg_owt abseil-cpp ada libavif libdispatch libheif libjpeg-turbo libjxl
    libpipewire libxcb libxcomposite libxdamage libxext libxfixes libxkbcommon
    libxrandr libxtst openh264 openssl pipewire rnnoise zlib glib2
    hicolor-icon-theme libx11
)

step() { printf '\n\033[1;33m>> %s\033[0m\n' "$1"; }
ok()   { printf '\033[1;32m[OK]\033[0m %s\n' "$1"; }
warn() { printf '\033[1;33m[WARN]\033[0m %s\n' "$1"; }
err()  { printf '\033[1;31m[X]\033[0m %s\n' "$1" >&2; }

install_system_deps() {
    if command -v pacman >/dev/null 2>&1; then
        step "Checking system dependencies (pacman)"
        local missing=()
        for pkg in "${PACMAN_PACKAGES[@]}"; do
            pacman -Qq "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
        done
        if [ "${#missing[@]}" -gt 0 ]; then
            echo "Installing: ${missing[*]}"
            sudo pacman -S --needed "${missing[@]}"
        else
            ok "All required packages already installed."
        fi
    else
        warn "pacman not found - install the equivalent of: ${PACMAN_PACKAGES[*]}"
    fi
}

detect_mold() {
    if command -v mold >/dev/null 2>&1; then
        MOLD_AVAILABLE=1
        ok "mold found, will use it for linking."
    else
        MOLD_AVAILABLE=0
        warn "mold not found - final linking will use the default linker and can need 8+ GB RAM/swap. Install 'mold' to avoid this."
    fi
}

ensure_submodules() {
    step "Checking submodules"
    if git -C "$REPO_ROOT" submodule status | grep -q '^-'; then
        git -C "$REPO_ROOT" submodule update --init --recursive
    else
        ok "Submodules already initialized."
    fi
}

# On Linux, tde2e is always resolved via find_package(tde2e) regardless of
# DESKTOP_APP_USE_PACKAGED - there is no bundled-source fallback for it.
build_and_install_tde2e() {
    local deps_prefix="$1"
    local tde2e_cmake_dir="$deps_prefix/lib/cmake/tde2e"

    if [ -f "$tde2e_cmake_dir/tde2eConfig.cmake" ]; then
        ok "tde2e already installed at $deps_prefix"
        return
    fi

    step "Building tde2e (E2E calls library from tdlib/td)"

    local td_commit
    td_commit="$(awk '/^  tde2e:/{f=1; next} f && /source-commit:/{print $2; exit}' "$REPO_ROOT/snap/snapcraft.yaml")"
    td_commit="${td_commit:-51743dfd01dff6179e2d8f7095729caa4e2222e9}"
    echo "Using tdlib/td commit: $td_commit"

    local td_src_dir="$REPO_ROOT/out/_deps/td"
    if [ ! -d "$td_src_dir/.git" ]; then
        mkdir -p "$(dirname "$td_src_dir")"
        git clone https://github.com/tdlib/td.git "$td_src_dir"
    fi
    git -C "$td_src_dir" checkout "$td_commit"

    cmake -S "$td_src_dir" -B "$td_src_dir/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$deps_prefix" \
        -DTD_E2E_ONLY=ON
    cmake --build "$td_src_dir/build" -j"$(nproc)"
    cmake --install "$td_src_dir/build"
}

# Sets API_ID / API_HASH. Falls back to the same test credentials
# scripts/build-windows.ps1 uses. Copy api_credentials.local.sh.example to
# override with your own.
resolve_api_credentials() {
    API_ID="17349"
    API_HASH="344583e45741c457fe1862106095a5eb"
    local f
    for f in "$REPO_ROOT/api_credentials.local.sh" "$(dirname "$REPO_ROOT")/api_credentials.local.sh"; do
        if [ -f "$f" ]; then
            # shellcheck disable=SC1090
            source "$f"
            API_ID="$TDESKTOP_API_ID"
            API_HASH="$TDESKTOP_API_HASH"
            ok "Using API credentials from $f"
            return
        fi
    done
}

configure_project() {
    local configuration="$1"
    local deps_prefix="$2"

    step "Configuring CMake ($configuration)"
    resolve_api_credentials
    detect_mold

    local cmake_args=(
        -B "$REPO_ROOT/out" -G Ninja
        -DCMAKE_BUILD_TYPE="$configuration"
        -DCMAKE_PREFIX_PATH="$deps_prefix"
        -DDESKTOP_APP_USE_PACKAGED=ON
        -DTDESKTOP_API_ID="$API_ID"
        -DTDESKTOP_API_HASH="$API_HASH"
    )
    if [ "${MOLD_AVAILABLE:-0}" -eq 1 ]; then
        cmake_args+=(
            -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=mold
            -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=mold
            -DCMAKE_MODULE_LINKER_FLAGS=-fuse-ld=mold
        )
    fi
    cmake "${cmake_args[@]}"
}

build_project() {
    step "Building"
    cmake --build "$REPO_ROOT/out" -j"$(nproc)"
    ok "Build finished: $REPO_ROOT/out/OwpenGram"
}

# --- portable build (Docker, Rocky Linux 8 baseline) -------------------------
#
# Produces a binary that runs on essentially any Linux from the last ~6 years
# (Ubuntu 18.04+, Debian 10+, ...) instead of one tied to this machine's exact
# library versions. Output goes to out-docker/ (kept separate from out/, which
# belongs to the native packaged-mode build) so the two never clobber each other.

DOCKER_IMAGE_TAG="tdesktop:centos_env"

ensure_docker_image() {
    if docker image inspect "$DOCKER_IMAGE_TAG" >/dev/null 2>&1; then
        ok "Docker image $DOCKER_IMAGE_TAG already built."
        return
    fi

    step "Building Docker image $DOCKER_IMAGE_TAG (Rocky Linux 8 - portable baseline, one-time, long)"

    if ! python3 -c "import jinja2" >/dev/null 2>&1; then
        if command -v pacman >/dev/null 2>&1; then
            sudo pacman -S --needed python-jinja
        else
            err "Python module 'jinja2' not found - install it (e.g. pip install --user jinja2)."
            return 1
        fi
    fi

    # The Dockerfile has ~15 independent stages (Qt6, WebRTC, openssl, ada, ...),
    # each doing its own `cmake --build`/LTO compile with no job cap by default.
    # On a normal workstation (not a 32+ core build server) that OOM-kills
    # cc1plus midway - some individual translation units (Qt's qmldom AST
    # code in particular) are heavy enough to OOM even mostly alone. Two
    # built-in template knobs fix this at the source:
    #   - JOBS sets CMAKE_BUILD_PARALLEL_LEVEL for every stage (real -j cap,
    #     not just a CPU quota that job schedulers ignore).
    #   - LTO=false drops -flto=auto -ffat-lto-objects, which roughly doubles
    #     per-file compile memory (keeps both LTO bytecode and regular object
    #     code in memory at once).
    # A dedicated builder with max-parallelism=1 additionally keeps two
    # whole stages (e.g. Qt and WebRTC) from running concurrently.
    local builder_name="owpengram-portable-builder"
    if ! docker buildx inspect "$builder_name" >/dev/null 2>&1; then
        step "Creating a builder ($builder_name) that only runs one stage at a time"
        local buildkitd_conf
        buildkitd_conf="$(mktemp)"
        cat > "$buildkitd_conf" <<'EOF'
[worker.oci]
  max-parallelism = 1
EOF
        docker buildx create --name "$builder_name" --driver docker-container --config "$buildkitd_conf"
        rm -f "$buildkitd_conf"
    fi

    # nproc/2 still OOM-killed on a Qt precompiled-header generation (PCH
    # compiles are unusually memory-hungry, more so than a typical TU). Size
    # this off actual RAM instead of core count: ~4 GB/job covers the worst
    # PCH/template-heavy files we've hit so far, with headroom for the OS.
    local total_ram_gb build_jobs
    total_ram_gb=$(( $(awk '/MemTotal/{print $2}' /proc/meminfo) / 1024 / 1024 ))
    build_jobs=$(( total_ram_gb / 4 ))
    if [ "$build_jobs" -lt 1 ]; then build_jobs=1; fi
    if [ "$build_jobs" -gt "$(nproc)" ]; then build_jobs="$(nproc)"; fi

    local dockerfile_dir="$REPO_ROOT/Telegram/build/docker/centos_env"
    (cd "$dockerfile_dir" && JOBS="$build_jobs" LTO= python3 gen_dockerfile.py > /tmp/owpengram-centos_env.Dockerfile)
    docker buildx build --builder "$builder_name" --load \
        -t "$DOCKER_IMAGE_TAG" -f /tmp/owpengram-centos_env.Dockerfile "$dockerfile_dir"
    rm -f /tmp/owpengram-centos_env.Dockerfile
}

build_via_docker() {
    local configuration="$1"
    local docker_out_dir="$REPO_ROOT/out-docker"

    ensure_submodules
    ensure_docker_image
    resolve_api_credentials
    mkdir -p "$docker_out_dir"

    step "Building via Docker ($configuration) - live output below"

    local tty_flags=()
    if [ -t 1 ]; then tty_flags=(-it); fi

    local config_env=()
    if [ "$configuration" = "Debug" ]; then config_env=(-e "CONFIG=Debug"); fi

    # build.sh's `cmake --build` has no job cap of its own, so ninja defaults
    # to nproc-wide parallelism. Unlike ensure_docker_image's ~15 independent
    # base-image stages, this is one flat build of the whole project - on a
    # normal workstation that's still enough concurrent heavy TUs (MTProto
    # scheme, style files, media viewer) to blow past RAM with no swap
    # cushion, and the kernel's OOM killer can take out dockerd itself
    # instead of just the compiler. Same RAM/4 heuristic as the image build.
    local total_ram_gb build_jobs
    total_ram_gb=$(( $(awk '/MemTotal/{print $2}' /proc/meminfo) / 1024 / 1024 ))
    build_jobs=$(( total_ram_gb / 4 ))
    if [ "$build_jobs" -lt 1 ]; then build_jobs=1; fi
    if [ "$build_jobs" -gt "$(nproc)" ]; then build_jobs="$(nproc)"; fi

    # The default bfd `ld` OOM-kills on the final link even with 11+ GB RAM
    # (same issue as the native build, just bigger). lld is already bundled
    # in this image (no mold), and is far lighter on memory.
    #
    # These -D...LINKER_FLAGS overrides *replace* CMAKE_*_LINKER_FLAGS rather
    # than extend it, which drops the image's own $LDFLAGS (normally seeded
    # into the _INIT variant at first configure). Several system libraries
    # here (EGL, GL, ...) are only linkable via Implib.so-generated stub
    # archives (e.g. /usr/local/lib64/libEGL.a) that dlopen() the real .so at
    # runtime - their trampoline resolvers need -ldl/-pthread from $LDFLAGS to
    # link at all. Without it, Qt6Gui's HAVE_EGL check fails to link and the
    # whole configure aborts. Reproduce $LDFLAGS here explicitly instead of
    # relying on _INIT, since a -D on the command line skips that entirely.
    local base_ldflags="-static-libstdc++ -static-libgcc -static-libasan -pthread -Wl,--push-state,--no-as-needed,-ldl,--pop-state -Wl,--as-needed -Wl,-z,muldefs"
    local linker_flags="$base_ldflags -fuse-ld=lld -Wl,--allow-multiple-definition"
    docker run --rm "${tty_flags[@]}" \
        -u "$(id -u)" \
        -v "$REPO_ROOT:/usr/src/tdesktop" \
        -v "$docker_out_dir:/usr/src/tdesktop/out" \
        "${config_env[@]}" \
        -e "CMAKE_BUILD_PARALLEL_LEVEL=$build_jobs" \
        "$DOCKER_IMAGE_TAG" \
        /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
        -D TDESKTOP_API_ID="$API_ID" \
        -D TDESKTOP_API_HASH="$API_HASH" \
        -D CMAKE_EXE_LINKER_FLAGS="$linker_flags" \
        -D CMAKE_SHARED_LINKER_FLAGS="$linker_flags" \
        -D CMAKE_MODULE_LINKER_FLAGS="$linker_flags"

    local binary_path="$docker_out_dir/$configuration/OwpenGram"

    # The image bakes in -g/-gdwarf64 even for Release (so official builds can
    # symbolicate crash reports) - that alone bloats an unstripped binary from
    # ~250 MB to 14+ GB. Strip Release; keep Debug symbols for local debugging.
    if [ "$configuration" = "Release" ] && [ -f "$binary_path" ]; then
        step "Stripping debug symbols (Release only)"
        strip "$binary_path"
    fi

    ok "Build finished: $binary_path"
}
