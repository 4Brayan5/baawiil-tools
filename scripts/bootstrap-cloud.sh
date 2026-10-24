#!/usr/bin/env bash
# Debian 13 cloud build. Windows runtime validation is a separate requirement.
set -euo pipefail
baawiil_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$baawiil_root"
mkdir -p .deps /tmp/baawiil-apt/etc/parts /tmp/baawiil-apt/lists/partial /tmp/baawiil-apt/cache/archives/partial
cat > /tmp/baawiil-apt/config <<'APT'
Dir::Etc "/tmp/baawiil-apt/etc";
Dir::Etc::parts "/tmp/baawiil-apt/etc/parts";
Dir::State::lists "/tmp/baawiil-apt/lists";
Dir::State::status "/var/lib/dpkg/status";
Dir::Cache "/tmp/baawiil-apt/cache";
Debug::NoLocking "true";
APT
cat > /tmp/baawiil-apt/etc/sources.list <<'APT'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
APT
run_logged() {
    local baawiil_log="$1"
    shift
    "$@" > "$baawiil_log" 2>&1 || { local baawiil_status=$?; tail -40 "$baawiil_log"; return "$baawiil_status"; }
}
if ! test -x /tmp/baawiil-build-tools/cmake/data/bin/cmake; then
    python3 -m pip install --target /tmp/baawiil-build-tools cmake==3.31.6
fi
run_logged /tmp/baawiil-apt-update.log env APT_CONFIG=/tmp/baawiil-apt/config /usr/bin/apt-get update
run_logged /tmp/baawiil-apt-download.log env APT_CONFIG=/tmp/baawiil-apt/config /usr/bin/apt-get --download-only --no-install-recommends --yes install qt6-base-dev libqt6sql6-sqlite mingw-w64-x86-64-dev g++-mingw-w64-x86-64-posix nsis nsis-common
python3 - <<'PY'
from pathlib import Path
import subprocess
for package in Path('/tmp/baawiil-apt/cache/archives').glob('*.deb'):
    subprocess.run(['dpkg-deb', '-x', str(package), '/tmp/baawiil-toolchain'], check=True)
base=Path('/tmp/baawiil-toolchain/usr/lib/x86_64-linux-gnu')
# Reuse installed host runtime libraries; target Windows libraries are built below.
for pattern in ['libQt6*.so*','libOpenGL.so*','libGLX.so*','libGL.so*','libEGL.so*']:
    for library in Path('/usr/lib/x86_64-linux-gnu').glob(pattern):
        target=base/library.name
        if not target.exists() and not target.is_symlink(): target.symlink_to(library)
PY
if ! test -e .deps/qtbase; then
    git clone --depth 1 --branch v6.8.2 https://github.com/qt/qtbase.git .deps/qtbase
fi
test "$(git -C .deps/qtbase rev-parse HEAD)" = f1136de66638060b8a1ab9bc0cdf1a91dcb5ec01
git -C .deps/qtbase diff --quiet
git -C .deps/qtbase diff --cached --quiet
cat > /tmp/baawiil-mingw.cmake <<CMAKE
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER /tmp/baawiil-toolchain/usr/bin/x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER /tmp/baawiil-toolchain/usr/bin/x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER /tmp/baawiil-toolchain/usr/bin/x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /tmp/baawiil-toolchain/usr/x86_64-w64-mingw32 "${baawiil_root}/.deps/qt-windows")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_RC_FLAGS "--preprocessor=/tmp/baawiil-toolchain/usr/bin/x86_64-w64-mingw32-gcc-posix --preprocessor-arg=-E --preprocessor-arg=-xc --preprocessor-arg=-DRC_INVOKED -I/tmp/baawiil-toolchain/usr/x86_64-w64-mingw32/include")
CMAKE
baawiil_cmake=/tmp/baawiil-build-tools/cmake/data/bin/cmake
export PATH="/tmp/baawiil-toolchain/usr/bin:$PATH"
run_logged /tmp/baawiil-qt-configure.log "$baawiil_cmake" -S .deps/qtbase -B .deps/qt-build -G 'Unix Makefiles' -DCMAKE_TOOLCHAIN_FILE=/tmp/baawiil-mingw.cmake -DQT_HOST_PATH=/tmp/baawiil-toolchain/usr -DQT_HOST_PATH_CMAKE_DIR=/tmp/baawiil-toolchain/usr/lib/x86_64-linux-gnu/cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$baawiil_root/.deps/qt-windows" -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF -DFEATURE_opengl=OFF -DFEATURE_dynamicgl=OFF -DFEATURE_vulkan=OFF -DFEATURE_dbus=OFF -DFEATURE_system_sqlite=OFF -DFEATURE_sql_mysql=OFF -DFEATURE_sql_psql=OFF -DFEATURE_sql_odbc=OFF -DFEATURE_sql_oci=OFF -DFEATURE_sql_ibase=OFF -DFEATURE_icu=OFF
run_logged /tmp/baawiil-qt-build.log "$baawiil_cmake" --build .deps/qt-build --parallel 3
run_logged /tmp/baawiil-qt-install.log "$baawiil_cmake" --install .deps/qt-build
run_logged /tmp/baawiil-host-configure.log "$baawiil_cmake" -S . -B build -DBAAWIIL_BUILD_APP=OFF -DBAAWIIL_QT_TESTS=ON -DBAAWIIL_UI_TESTS=ON -DCMAKE_PREFIX_PATH='/tmp/baawiil-toolchain/usr;/tmp/baawiil-toolchain/usr/lib/x86_64-linux-gnu/cmake' -DOPENGL_INCLUDE_DIR=/tmp/baawiil-toolchain/usr/include
run_logged /tmp/baawiil-host-build.log "$baawiil_cmake" --build build --parallel 3
QT_PLUGIN_PATH=/tmp/baawiil-toolchain/usr/lib/x86_64-linux-gnu/qt6/plugins LD_LIBRARY_PATH=/tmp/baawiil-toolchain/usr/lib/x86_64-linux-gnu /tmp/baawiil-build-tools/cmake/data/bin/ctest --test-dir build --output-on-failure
run_logged /tmp/baawiil-app-configure.log "$baawiil_cmake" -S . -B .deps/app-windows -DCMAKE_TOOLCHAIN_FILE=/tmp/baawiil-mingw.cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$baawiil_root/.deps/qt-windows" -DQT_HOST_PATH=/tmp/baawiil-toolchain/usr -DQT_HOST_PATH_CMAKE_DIR=/tmp/baawiil-toolchain/usr/lib/x86_64-linux-gnu/cmake -DBAAWIIL_QT_TESTS=ON
run_logged /tmp/baawiil-app-windows-build.log "$baawiil_cmake" --build .deps/app-windows --parallel 3
printf 'Linux component checks passed; Windows binaries built. Windows 11 runtime tests remain required.\n'
