#!/usr/bin/env bash
# On-device build for aarch64 hosts (phone/Termux) where ndk-build cannot run.
# Uses Termux clang++ with headers + stub libs from the NDK sysroot.
# Mirrors jni/Android.mk (sources, flags, -pie executable) and emits both
# out/arm64-v8a/bycmd-1.0.0 and out/arm64-v8a/bycmd-1.0.0.so (same bytes).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
NDK="${ANDROID_NDK_HOME:-/opt/android-ndk-r30}"
SYS="$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot"
ARCHLIB="$SYS/usr/lib/aarch64-linux-android"
UNWIND="$NDK/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/21/lib/linux/aarch64/libunwind.a"
[[ -d "$SYS" ]] || { echo "NDK sysroot not found: $SYS" >&2; exit 1; }

OBJ="$ROOT/build/obj-local"
mkdir -p "$OBJ" "$ROOT/out/arm64-v8a"

FLAGS="--target=aarch64-linux-android23 --sysroot=$SYS -stdlib=libc++ \
-std=c++17 -fno-rtti -fno-exceptions -fvisibility=hidden -fvisibility-inlines-hidden \
-Oz -ffunction-sections -fdata-sections -fomit-frame-pointer \
-Wno-error=format-security -fmerge-all-constants -fno-ident \
-I$ROOT/includes -I$ROOT/includes/fonts -I$ROOT/includes/internal \
-I$ROOT/includes/internal/ImGui -I$ROOT/includes/internal/ImGui/backends \
-I$ROOT/includes/draw/ImGui -I$ROOT/includes/draw/ImGui/backends \
-I$ROOT/src -I$ROOT/src/ui"

i=0
# shellcheck disable=SC2086
for f in src/main.cpp src/ui/menu.cpp src/ui/bar.cpp src/ui/widgets/widgets.cpp \
         src/func/visuals.cpp src/func/ground_pulse.cpp src/func/tracer.cpp \
         src/combat/combat.cpp src/input/inject.cpp src/protect/oxorany.cpp \
         includes/draw/Android_draw/draw.cpp includes/draw/Android_touch/Touch.cpp \
         includes/draw/ImGui/imgui.cpp includes/draw/ImGui/imgui_draw.cpp \
         includes/draw/ImGui/imgui_tables.cpp includes/draw/ImGui/imgui_widgets.cpp \
         includes/draw/ImGui/backends/imgui_impl_android.cpp \
         includes/draw/ImGui/backends/imgui_impl_opengl3.cpp; do
  i=$((i+1))
  # shellcheck disable=SC2086
  clang++ $FLAGS -c "$ROOT/$f" -o "$OBJ/f$i.o"
  echo "ok $i/18: $f"
done

# shellcheck disable=SC2086
clang++ --target=aarch64-linux-android23 --sysroot=$SYS -nostdlib++ -unwindlib=none \
  "$OBJ"/f*.o "$ARCHLIB/libc++_static.a" "$ARCHLIB/libc++abi.a" "$UNWIND" \
  -Wl,--gc-sections -Wl,--strip-all -Wl,--build-id=none -Wl,--no-undefined \
  -pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack \
  -llog -landroid -lEGL -lGLESv3 \
  -o "$ROOT/out/arm64-v8a/bycmd-1.0.0"

cp -f "$ROOT/out/arm64-v8a/bycmd-1.0.0" "$ROOT/out/arm64-v8a/bycmd-1.0.0.so"
echo "Built: $ROOT/out/arm64-v8a/bycmd-1.0.0"
echo "Built: $ROOT/out/arm64-v8a/bycmd-1.0.0.so"
