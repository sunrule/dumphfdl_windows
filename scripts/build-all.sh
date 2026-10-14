#!/usr/bin/env bash
#
# build-all.sh - сборка dumphfdl для Windows (MinGW) во всех вариантах:
#
#   x86 (32-бит):  dumphfdl.exe, dumphfdl.dll  (+ рантайм-DLL)
#   x64 (64-бит):  dumphfdl.exe, dumphfdl.dll  (+ рантайм-DLL)
#
# Скрипт:
#   1. Устанавливает зависимости через pacman (MSYS2) - те, что есть
#   2. Собирает из исходников то, чего нет в pacman (для i686)
#   3. Собирает все 4 бинарника
#   4. Формирует готовые дистрибутивы в dist/x86 и dist/x64
#
# Требования:
#   - MSYS2 (https://www.msys2.org/) с пакетами:
#       pacman -S git mingw-w64-i686-gcc mingw-w64-x86_64-gcc \
#                  mingw-w64-i686-cmake mingw-w64-x86_64-cmake \
#                  mingw-w64-i686-ninja mingw-w64-x86_64-ninja \
#                  mingw-w64-i686-pkg-config mingw-w64-x86_64-pkg-config \
#                  mingw-w64-i686-sqlite3 mingw-w64-x86_64-sqlite3 \
#                  curl
#   - Запускать из MSYS2 MinGW x64 shell (не MSYS shell!)
#
# Использование:
#   ./scripts/build-all.sh              # полная сборка всего
#   ./scripts/build-all.sh --deps-only  # только зависимости
#   ./scripts/build-all.sh --no-deps    # без пересборки зависимостей
#
set -euo pipefail

# ---- версии зависимостей (фиксированы) ----
FFTW_VERSION=3.3.11
FFTW_URL="https://www.fftw.org/fftw-${FFTW_VERSION}.tar.gz"
LIBCONFIG_VERSION=1.8.2
LIBCONFIG_URL="https://hyperrealm.github.io/libconfig/dist/libconfig-${LIBCONFIG_VERSION}.tar.gz"

# ---- пути ----
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
# Windows-путь к проекту (MSYS /c/... не понимается нативными тулами)
PROJECT_DIR_WIN="$(cd "$PROJECT_DIR" && pwd -W)"
BUILD_ROOT="${BUILD_ROOT:-$PROJECT_DIR/build}"
DEPS_DIR="$BUILD_ROOT/deps-src"
PREFIX_X64="${PREFIX_X64:-$BUILD_ROOT/prefix-x64}"   # не используется (x64 из pacman)
PREFIX_X86="$BUILD_ROOT/prefix-x86"                   # i686-зависимости из исходников
X64_PREFIX="$BUILD_ROOT/prefix-x64"                   # x64 liquid/libacars (из исходников)
DIST_DIR="$PROJECT_DIR/dist"

# MSYS2/MinGW: ninja запускает компилятор через cmd.exe, а collect2 по
# умолчанию кладёт temp-файлы в C:\Windows (Permission denied).
# Выставляем TMP/TEMP в доступную директорию сборки.
TMP_BUILD_DIR="$BUILD_ROOT/tmp"
mkdir -p "$TMP_BUILD_DIR" 2>/dev/null || true
TMP_WIN="$(cd "$TMP_BUILD_DIR" 2>/dev/null && pwd -W)" || TMP_WIN="$TMP_BUILD_DIR"
export TMPDIR="$TMP_BUILD_DIR"
export TMP="$TMP_WIN"
export TEMP="$TMP_WIN"

# ---- цвета/логи ----
# pacman живёт в usr/bin MSYS2, который может отсутствовать в PATH.
# Пробуем несколько типовых расположений.
PACMAN="$(command -v pacman 2>/dev/null || true)"
if [ -z "$PACMAN" ]; then
    for p in /usr/bin/pacman /c/msys64/usr/bin/pacman /mingw64/../usr/bin/pacman; do
        [ -x "$p" ] && PACMAN="$p" && break
    done
fi
if [ -z "$PACMAN" ]; then
    die "pacman не найден. Запустите скрипт из MSYS2 (MinGW x64) shell."
fi

log()  { echo -e "\033[1;32m==>\033[0m $*"; }
warn() { echo -e "\033[1;33m[warn]\033[0m $*" >&2; }
die()  { echo -e "\033[1;31m[error]\033[0m $*" >&2; exit 1; }

# ninja обёртка: запуск через cmd.exe с выставленными TMP/TEMP
# (MSYS не пробрасывает эти переменные в нативные процессы корректно)
ninja() {
    local args="$*"
    local tmpwin
    tmpwin="$(cd "$TMP_BUILD_DIR" 2>/dev/null && pwd -W)" || tmpwin="$TMP_BUILD_DIR"
    C:/Windows/System32/cmd.exe /C "set TMP=$tmpwin&& set TEMP=$tmpwin&& $(command -v ninja) $args"
}

DEPS_ONLY=0
SKIP_DEPS=0
for arg in "$@"; do
    case "$arg" in
        --deps-only) DEPS_ONLY=1 ;;
        --no-deps)   SKIP_DEPS=1 ;;
        *) die "Unknown option: $arg" ;;
    esac
done

# ===========================================================================
# 1. Зависимости для x64 (из pacman MSYS2)
# ===========================================================================
install_x64_deps() {
    log "Проверка x64-зависимостей (pacman)"
    local pkgs=(
        mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
        mingw-w64-x86_64-pkg-config
        mingw-w64-x86_64-fftw mingw-w64-x86_64-libconfig
        mingw-w64-x86_64-sqlite3 mingw-w64-x86_64-zlib
    )
    local missing=()
    for p in "${pkgs[@]}"; do
        "$PACMAN" -Q "$p" >/dev/null 2>&1 || missing+=("$p")
    done
    if [ ${#missing[@]} -gt 0 ]; then
        log "Установка: ${missing[*]}"
        "$PACMAN" -S --noconfirm --needed "${missing[@]}" \
            || die "pacman install failed (нужны права администратора?)"
    else
        log "Все x64-зависимости уже установлены"
    fi
}

# ===========================================================================
# 2. Зависимости для i686
#    fftw3f, libconfig, libacars, liquid-dsp в pacman для i686 отсутствуют
#    (fftw/libconfig есть только для x86_64), поэтому собираем из исходников.
# ===========================================================================

download_and_extract() {
    local url="$1" name="$2"
    if [ -d "$DEPS_DIR/$name" ]; then
        log "$name уже распакован"
        return 0
    fi
    mkdir -p "$DEPS_DIR"
    local archive="$DEPS_DIR/$name.tar.gz"
    if [ ! -f "$archive" ]; then
        log "Скачивание $url"
        curl -fsSL --connect-timeout 30 -o "$archive" "$url" \
            || die "не удалось скачать $url"
    fi
    log "Распаковка $name"
    tar xzf "$archive" -C "$DEPS_DIR" || die "не удалось распаковать $name"
}

build_dep_i686() {
    local name="$1" src="$2"; shift 2
    log "Сборка $name (i686)"
    rm -rf "$src/build-i686"
    cmake -G Ninja -B "$src/build-i686" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_C_COMPILER=C:/msys64/mingw32/bin/gcc.exe \
        -DCMAKE_CXX_COMPILER=C:/msys64/mingw32/bin/g++.exe \
        -DCMAKE_FIND_ROOT_PATH=C:/msys64/mingw32 \
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
        -DCMAKE_INSTALL_PREFIX="$PREFIX_X86" \
        "$@" "$src" || die "cmake $name failed"
    ninja -C "$src/build-i686" || die "build $name failed"
    ninja -C "$src/build-i686" install || die "install $name failed"
}

build_x86_deps() {
    log "Сборка i686-зависимостей из исходников в $PREFIX_X86"
    export PATH="/c/msys64/mingw32/bin:$PATH"

    # libconfig 1.8.2: в тарболе отсутствует win32/stdint.h (баг дистрибутива)
    # и используется fsync(), которого нет в заголовках MinGW.
    # Создаём шим и подменяем fsync на _commit.
    download_and_extract "$LIBCONFIG_URL" "libconfig-${LIBCONFIG_VERSION}"
    local lc="$DEPS_DIR/libconfig-${LIBCONFIG_VERSION}"
    mkdir -p "$lc/lib/win32"
    if [ ! -f "$lc/lib/win32/stdint.h" ]; then
        cat > "$lc/lib/win32/stdint.h" <<'EOF'
/* Shim for MinGW builds: libconfig's win32/stdint.h is missing from the
 * 1.8.2 distribution tarball. MinGW provides a complete stdint.h. */
#ifndef _LIBCONFIG_WIN32_STDINT_SHIM
#define _LIBCONFIG_WIN32_STDINT_SHIM
#include <stdint.h>
#endif
EOF
    fi
    build_dep_i686 "libconfig" "$lc" \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF \
        -DCMAKE_C_FLAGS="-Dfsync=_commit"

    # libconfig не ставит .pc на Windows - создаём вручную
    mkdir -p "$PREFIX_X86/lib/pkgconfig"
    cat > "$PREFIX_X86/lib/pkgconfig/libconfig.pc" <<EOF
prefix=$PREFIX_X86
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: libconfig
Description: C Configuration File Library
Version: ${LIBCONFIG_VERSION}
Libs: -L\${libdir} -lconfig
Cflags: -I\${includedir}
EOF

    # fftw3f (нужны только float + threads)
    download_and_extract "$FFTW_URL" "fftw-${FFTW_VERSION}"
    build_dep_i686 "fftw" "$DEPS_DIR/fftw-${FFTW_VERSION}" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DENABLE_FLOAT=ON -DENABLE_DOUBLE=OFF -DENABLE_LONG_DOUBLE=OFF \
        -DENABLE_THREADS=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF

    # libacars (с поддержкой zlib для MIAM)
    # Исходники: https://github.com/szpajder/libacars (v2.2.1)
    if [ ! -d "$DEPS_DIR/libacars" ]; then
        log "Клонирование libacars v2.2.1"
        git clone --depth 1 --branch v2.2.1 \
            https://github.com/szpajder/libacars.git "$DEPS_DIR/libacars" \
            || die "git clone libacars failed"
    fi
    build_dep_i686 "libacars" "$DEPS_DIR/libacars" \
        -DBUILD_SHARED_LIBS=OFF \
        -DZLIB_INCLUDE_DIR=C:/msys64/mingw32/include \
        -DZLIB_LIBRARY=C:/msys64/mingw32/lib/libz.a
    # install положил только DLL-вариант; копируем статическую библиотеку
    cp "$DEPS_DIR/libacars/build-i686/libacars/libacars-2.a" "$PREFIX_X86/lib/" \
        || die "copy libacars-2.a failed"

    # liquid-dsp 1.8.3
    # Исходники: https://github.com/jgaeddert/liquid-dsp
    if [ ! -d "$DEPS_DIR/liquid-dsp" ]; then
        log "Клонирование liquid-dsp v1.8.3"
        git clone --depth 1 --branch v1.8.3 \
            https://github.com/jgaeddert/liquid-dsp.git "$DEPS_DIR/liquid-dsp" \
            || die "git clone liquid-dsp failed"
    fi
    patch_liquid_timer_c
    build_dep_i686 "liquid-dsp" "$DEPS_DIR/liquid-dsp" \
        -DBUILD_STATIC_LIBS=ON -DBUILD_SHARED_LIBS=OFF \
        -DENABLE_LOGGING=OFF -DENABLE_TIMESTAMPS=OFF \
        -DBUILD_EXAMPLES=OFF -DBUILD_AUTOTESTS=OFF -DBUILD_BENCHMARKS=OFF

    log "i686-зависимости готовы в $PREFIX_X86"
}

# ===========================================================================
# 3. Сборка проекта
# ===========================================================================

# Общие флаги, совместимые со старым и новым GCC.
# -std=c11 -pthread -ffast-math -fno-common - исторически используются
# проектом; без них новый GCC превращает предупреждения о несовместимых
# указателях (la_list_free_full в main.c) в ошибки.
CFLAGS="-std=c11 -pthread -ffast-math -fno-common -D_XOPEN_SOURCE=600 -DLIBCONFIG_STATIC"

build_variant() {
    local arch="$1" build_dir="$2" prefix="$3"; shift 3
    local extra=("$@")

    log "Конфигурация $arch ($build_dir)"
    rm -rf "$build_dir"
    if [ "$arch" = "x86" ]; then
        # MSYS2: mingw32-пакетов pkg-config может не быть - берём
        # msys-версию (/usr/bin) или любую доступную.
        export PATH="/c/msys64/usr/bin:/c/msys64/mingw32/bin:$PATH"
        local pkgdir
        pkgdir="$(cd "$PREFIX_X86/lib/pkgconfig" 2>/dev/null && pwd -W)" || pkgdir="$PREFIX_X86/lib/pkgconfig"
        export PKG_CONFIG_PATH="$pkgdir"
        cmake -G Ninja -B "$build_dir" -S "$PROJECT_DIR_WIN" \
            -DCMAKE_TOOLCHAIN_FILE="$PROJECT_DIR_WIN/cmake/toolchain-mingw32.cmake" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_FLAGS="$CFLAGS" \
            -DBUILD_DLL=ON \
            -DI686_PREFIX="$PREFIX_X86" \
            -DLIQUIDDSP_INCLUDE_DIR="$PREFIX_X86/include" \
            -DLIQUIDDSP_LIBRARIES="$PREFIX_X86/lib/libliquid.a" \
            -DFFTW_INCLUDE_DIR="$PREFIX_X86/include" \
            -DFFTW_LIBRARIES="$PREFIX_X86/lib/libfftw3f.a" \
            -DACARS_INCLUDE_DIR="$PREFIX_X86/include/libacars-2" \
            -DACARS_LIBRARIES="$PREFIX_X86/lib/libacars-2.a" \
            -DCONFIG_INCLUDE_DIR="$PREFIX_X86/include" \
            -DCONFIG_LIBRARIES="$PREFIX_X86/lib/libconfig.a" \
            -DSQLite3_INCLUDE_DIR=C:/msys64/mingw32/include \
            -DSQLite3_LIBRARY=C:/msys64/mingw32/lib/libsqlite3.dll.a \
            "${extra[@]}" \
            || die "cmake $arch failed"
    else
        export PATH="/c/msys64/mingw64/bin:$PATH"
        local x64pkg
        x64pkg="$(cd "$X64_PREFIX/lib/pkgconfig" 2>/dev/null && pwd -W)" || x64pkg="$X64_PREFIX/lib/pkgconfig"
        export PKG_CONFIG_PATH="$x64pkg:$PKG_CONFIG_PATH"
        cmake -G Ninja -B "$build_dir" -S "$PROJECT_DIR_WIN" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_SYSTEM_NAME=Windows \
            -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe \
            -DCMAKE_C_FLAGS="$CFLAGS" \
            -DBUILD_DLL=ON \
            -DCMAKE_PREFIX_PATH="$X64_PREFIX" \
            -DLIQUIDDSP_INCLUDE_DIR="$X64_PREFIX/include" \
            -DLIQUIDDSP_LIBRARIES="$X64_PREFIX/lib/libliquid.a" \
            -DFFTW_INCLUDE_DIR=C:/msys64/mingw64/include \
            -DFFTW_LIBRARIES=C:/msys64/mingw64/lib/libfftw3f.a \
            -DACARS_INCLUDE_DIR="$X64_PREFIX/include/libacars-2" \
            -DACARS_LIBRARIES="$X64_PREFIX/lib/libacars-2.a" \
            -DCONFIG_INCLUDE_DIR=C:/msys64/mingw64/include \
            -DCONFIG_LIBRARIES=C:/msys64/mingw64/lib/libconfig.dll.a \
            -DSQLite3_INCLUDE_DIR=C:/msys64/mingw64/include \
            -DSQLite3_LIBRARY=C:/msys64/mingw64/lib/libsqlite3.dll.a \
            "${extra[@]}" \
            || die "cmake $arch failed"
    fi

    log "Сборка $arch"
    ninja -C "$build_dir" || die "ninja $arch failed"
}

# liquid-dsp и libacars для x64 (в pacman их нет, собираем из исходников).
# fftw/libconfig/sqlite/zlib для x64 берутся из pacman.
build_x64_deps() {
    if [ -f "$X64_PREFIX/lib/pkgconfig/liquid-dsp.pc" ] && \
       [ -f "$X64_PREFIX/lib/pkgconfig/libacars-2.pc" ]; then
        log "x64 liquid/libacars уже собраны ($X64_PREFIX)"
        return 0
    fi
    log "Сборка x64 liquid-dsp и libacars в $X64_PREFIX"
    export PATH="/c/msys64/mingw64/bin:$PATH"

    if [ ! -d "$DEPS_DIR/libacars" ]; then
        log "Клонирование libacars v2.2.1"
        git clone --depth 1 --branch v2.2.1 \
            https://github.com/szpajder/libacars.git "$DEPS_DIR/libacars" \
            || die "git clone libacars failed"
    fi
    rm -rf "$DEPS_DIR/libacars/build-x64"
    cmake -G Ninja -B "$DEPS_DIR/libacars/build-x64" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe \
        -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe \
        -DCMAKE_INSTALL_PREFIX="$X64_PREFIX" \
        -DBUILD_SHARED_LIBS=OFF \
        -DZLIB_INCLUDE_DIR=C:/msys64/mingw64/include \
        -DZLIB_LIBRARY=C:/msys64/mingw64/lib/libz.a \
        "$DEPS_DIR/libacars" || die "cmake libacars x64 failed"
    ninja -C "$DEPS_DIR/libacars/build-x64" || die "build libacars x64 failed"
    ninja -C "$DEPS_DIR/libacars/build-x64" install || die "install libacars x64 failed"
    # install кладёт только import-библиотеку; статическую копируем вручную
    # (libacars 2.2.1 с BUILD_SHARED_LIBS=OFF кладёт .a в build/libacars/)
    if [ -f "$DEPS_DIR/libacars/build-x64/libacars/libacars-2.a" ]; then
        cp "$DEPS_DIR/libacars/build-x64/libacars/libacars-2.a" "$X64_PREFIX/lib/" \
            || die "copy libacars-2.a failed"
    fi

    if [ ! -d "$DEPS_DIR/liquid-dsp" ]; then
        log "Клонирование liquid-dsp v1.8.3"
        git clone --depth 1 --branch v1.8.3 \
            https://github.com/jgaeddert/liquid-dsp.git "$DEPS_DIR/liquid-dsp" \
            || die "git clone liquid-dsp failed"
    fi
    patch_liquid_timer_c
    rm -rf "$DEPS_DIR/liquid-dsp/build-x64"
    cmake -G Ninja -B "$DEPS_DIR/liquid-dsp/build-x64" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe \
        -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe \
        -DCMAKE_INSTALL_PREFIX="$X64_PREFIX" \
        -DBUILD_STATIC_LIBS=ON -DBUILD_SHARED_LIBS=OFF \
        -DENABLE_LOGGING=OFF -DENABLE_TIMESTAMPS=OFF \
        -DBUILD_EXAMPLES=OFF -DBUILD_AUTOTESTS=OFF -DBUILD_BENCHMARKS=OFF \
        "$DEPS_DIR/liquid-dsp" || die "cmake liquid-dsp x64 failed"
    ninja -C "$DEPS_DIR/liquid-dsp/build-x64" || die "build liquid-dsp x64 failed"
    ninja -C "$DEPS_DIR/liquid-dsp/build-x64" install || die "install liquid-dsp x64 failed"
}

# liquid-dsp 1.8.3: src/core/src/timer.c использует struct rusage и
# sys/resource.h, которых нет в MinGW (liquid только хранит struct,
# getrusage() не вызывается). Подменяем include на шим.
patch_liquid_timer_c() {
    local lc="$DEPS_DIR/liquid-dsp/src/core/src/timer.c"
    [ -f "$lc" ] || return 0
    if grep -q "LIQUID_MINRESOURCE_SHIM" "$lc"; then
        return 0
    fi
    log "Патч timer.c для MinGW (sys/resource.h / getrusage)"
    PYTHON_CMD="$(command -v python3 || command -v python || true)"
    if [ -z "$PYTHON_CMD" ]; then
        die "нужен python для патча timer.c"
    fi
    "$PYTHON_CMD" - "$lc" <<'PYEOF'
import sys
p = sys.argv[1]
c = open(p, encoding='utf-8', errors='replace').read()
shim = """/* LIQUID_MINRESOURCE_SHIM: MinGW lacks sys/resource.h and getrusage().
 * liquid-dsp 1.8.3 reads ru_utime/ru_stime from struct rusage and calls
 * getrusage(RUSAGE_SELF, ...). Provide a POSIX-compatible struct plus a
 * stub so the file compiles on Windows. */
#ifndef LIQUID_MINRESOURCE_SHIM
#define LIQUID_MINRESOURCE_SHIM
#ifndef RUSAGE_SELF
#define RUSAGE_SELF 0
#endif
struct rusage {
    struct timeval ru_utime; /* user CPU time */
    struct timeval ru_stime; /* system CPU time */
    long ru_maxrss;
    long ru_ixrss;
    long ru_idrss;
    long ru_isrss;
    long ru_minflt;
    long ru_majflt;
    long ru_nswap;
    long ru_inblock;
    long ru_oublock;
    long ru_msgsnd;
    long ru_msgrcv;
    long ru_nsignals;
    long ru_nvcsw;
    long ru_nivcsw;
};
static inline int getrusage(int who, struct rusage *usage) {
    (void)who; (void)usage;
    return 0;
}
#endif
"""
c = c.replace('#include <sys/time.h>', '#include <sys/time.h>\n' + shim, 1)
c = c.replace('#include <sys/resource.h>', '/* removed for MinGW: see LIQUID_MINRESOURCE_SHIM */', 1)
open(p, 'w', encoding='utf-8').write(c)
PYEOF
}

collect_runtime_dlls() {
    local arch="$1" dest="$2"
    mkdir -p "$dest"
    if [ "$arch" = "x86" ]; then
        # i686: fftw/acars/liquid/libconfig вкомпилированы статически,
        # нужны только рантайм-DLL компилятора и sqlite/zlib.
        cp /c/msys64/mingw32/bin/libgcc_s_dw2-1.dll \
           /c/msys64/mingw32/bin/libwinpthread-1.dll \
           /c/msys64/mingw32/bin/zlib1.dll \
           /c/msys64/mingw32/bin/libsqlite3-0.dll \
           "$dest/"
    else
        # x64: liquid/acars/libconfig/fftw берутся как DLL из pacman/префикса.
        cp /c/msys64/mingw64/bin/libfftw3f_threads-3.dll \
           /c/msys64/mingw64/bin/libconfig-15.dll \
           /c/msys64/mingw64/bin/libsqlite3-0.dll \
           /c/msys64/mingw64/bin/libwinpthread-1.dll \
           /c/msys64/mingw64/bin/libgcc_s_seh-1.dll \
           /c/msys64/mingw64/bin/zlib1.dll \
           "$dest/"
    fi
}

# ===========================================================================
# main
# ===========================================================================
[ "$SKIP_DEPS" = "1" ] || install_x64_deps
[ "$SKIP_DEPS" = "1" ] || build_x64_deps
[ "$SKIP_DEPS" = "1" ] || build_x86_deps

if [ "$DEPS_ONLY" = "1" ]; then
    log "Готово (--deps-only)"
    exit 0
fi

build_variant "x64" "$BUILD_ROOT/dumphfdl-x64" "$PREFIX_X64"
build_variant "x86" "$BUILD_ROOT/dumphfdl-i686" "$PREFIX_X86"

# ---- дистрибутивы ----
log "Формирование дистрибутивов в $DIST_DIR"
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/x64" "$DIST_DIR/x86"

for arch in x64 x86; do
    if [ "$arch" = "x64" ]; then
        bdir="$BUILD_ROOT/dumphfdl-x64"
    else
        bdir="$BUILD_ROOT/dumphfdl-i686"
    fi
    cp "$bdir/src/dumphfdl.exe" "$DIST_DIR/$arch/"
    cp "$bdir/src/dumphfdl.dll" "$DIST_DIR/$arch/"
    collect_runtime_dlls "$arch" "$DIST_DIR/$arch"
done

log "Готово!"
echo
echo "Дистрибутивы:"
echo "  64-бит: $DIST_DIR/x64/  (dumphfdl.exe, dumphfdl.dll + рантайм-DLL)"
echo "  32-бит: $DIST_DIR/x86/  (dumphfdl.exe, dumphfdl.dll + рантайм-DLL)"
echo
echo "Для Delphi 7 используйте $DIST_DIR/x86/dumphfdl.dll"
echo "(примеры и документация: delphi/, doc/dll-usage.md)"
