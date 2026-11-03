# Super Mario War for Android

An Android build of upstream Super Mario War (SDL3) with handheld and Bluetooth
controller support. Player setup and controls: [docs/android.md](../docs/android.md).

## Building

Requirements: Android SDK (platform 35), NDK 27.2.12479018, JDK 17, CMake 3.24+, Ninja.

```sh
git submodule update --init --recursive
ANDROID_HOME=/path/to/android-sdk ./android/build.sh
```

The APK is written to `android/app/build/outputs/apk/debug/app-debug.apk`.
Every push to the repository also builds it on GitHub Actions (see the Actions tab); the
APK and its debug symbols are kept with each build for testing. A tested build becomes a
public release with **Actions -> Publish release -> Run workflow**: give it a tag (`v1.1`)
and the build's run ID (the number at the end of the build's URL). Release notes are read
from `docs/releases/<tag>.md` if that file exists.

`build.sh` cross-compiles the game, SDL3, SDL3_image and SDL3_mixer with the NDK, then
assembles the app with Gradle. SDL's Android Java layer and its native library come from
one checkout (`android/deps/SDL`) at the SDL3 release pinned in `cmake/BundledDeps.cmake`,
so they always match.

Debug builds are signed with `android/app/debug.keystore` (standard debug passwords) so
each new build installs over the previous one.

Host-side tests for touch and controller logic: `./android/test-touch.sh`.

## Credits

The Android groundwork (build setup, asset extraction, touch controls) comes from the
Android port by @doctormajid7-ux: https://github.com/doctormajid7-ux/supermariowar-android-2Pdualscreen

CRT filters: crt-lottes (public domain, Timothy Lottes), crt-easymode and
crt-easymode-halation (GPL, EasyMode)
from https://github.com/libretro/glsl-shaders, included unmodified.

