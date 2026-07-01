#!/usr/bin/env bash
set -euo pipefail

if [ $# -lt 1 ]; then
  echo "Error: preset is required"
  echo "Usage: $0 <preset>"
  exit 1
fi

PRESET=$1
BUILD_DIR=build_preset_$PRESET

CCACHE_ARGS=()
if command -v ccache >/dev/null 2>&1; then
  CCACHE_ARGS=(
    -DCMAKE_C_COMPILER_LAUNCHER=ccache
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
  )
fi

# Workaround: CMakeLists.txt injects -Werror for Release|RelWithDebInfo builds,
# which trips on warnings emitted by newer GCC on Ubuntu. Append -Wno-error to
# CMAKE_<LANG>_FLAGS_RELWITHDEBINFO so it lands after -Werror on the compile
# line and cancels it. Default RelWithDebInfo flags (-O2 -g -DNDEBUG) preserved.
if [ "$PRESET" = "release" ]; then
  cmake --preset "$PRESET" \
    "${CCACHE_ARGS[@]}" \
    -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O2 -g -DNDEBUG -Wno-error" \
    -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -g -DNDEBUG -Wno-error"
else
  cmake --preset "$PRESET" "${CCACHE_ARGS[@]}"
fi && \
cmake --build --preset "$PRESET" -j"$(nproc)" && \
cmake --install "$BUILD_DIR" && \
ln -sf "$BUILD_DIR/compile_commands.json" compile_commands.json
