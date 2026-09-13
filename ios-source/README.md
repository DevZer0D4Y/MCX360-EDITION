# MCX360 Edition: iOS source

Source for building **Minecraft: Xbox 360 Edition (Title Update 9)** as a native iOS app with the [ReXGlue](https://github.com/rexglue/rexglue-sdk) static recompilation SDK.

> [!IMPORTANT]
> **No game files are included, and none should ever be committed.** This repository contains only build scripts, the app shell and SDK changes. The game's code is recompiled on *your* machine from *your own* disc during the build, and the game data is copied onto *your* phone by you.

## What's in here

```
ios-source/
├── app/                      The iOS app project
│   ├── CMakeLists.txt
│   ├── CMakePresets.json
│   ├── mc360_manifest.toml   Codegen settings (function addresses only - no game code)
│   ├── generated/
│   │   └── rexglue.cmake     SDK build glue; codegen writes the recompiled game to generated/default/
│   ├── platform/ios/Info.plist
│   ├── src/                  App entry point and iOS setup (paths, settings, performance defaults)
│   └── game/                 ← YOUR game files go here (ignored by git)
├── sdk/
│   └── rexglue-sdk-ios.patch Changes to ReXGlue v0.10.0 needed for iOS
└── scripts/
    ├── make-ipa.sh           Package an unsigned .ipa for sideloading
    └── sign-and-install.sh   Sign with your own profile and install to a connected iPhone
```

---

## Requirements

- A **Mac with Apple silicon**
- **Xcode** with the iOS SDK installed (open it once to accept the license)
- **CMake** 3.25 or newer and **Ninja**, for example: `brew install cmake ninja`
- **git**, an internet connection (dependencies are downloaded during the build), and several GB of free disk space
- Your own **Minecraft: Xbox 360 Edition disc** and **Title Update 9**
- An iPhone on **iOS 16+** and a **Bluetooth controller** (there are no touch controls)

### Your game files must match exactly

The codegen settings in `mc360_manifest.toml` were made for one specific build of the game:

| Field | Required value |
|---|---|
| Title ID | `584111F7` |
| Media ID | `7CD33B56` |
| Version after TU | `0.0.10.1` (TU9) |

Other versions will fail at codegen or crash at runtime.

---

## 1. Build the patched ReXGlue SDK

```sh
git clone https://github.com/rexglue/rexglue-sdk.git
cd rexglue-sdk
git checkout c94f5eb                     # Release v0.10.0 - the patch is made against this
git submodule update --init --recursive
git apply /path/to/ios-source/sdk/rexglue-sdk-ios.patch
```

**Host tools (macOS).** Codegen runs on your Mac, so build the `rexglue` command-line tool first:

```sh
cmake --preset mac-arm64
cmake --build --preset mac-arm64-release --target rexglue
```

This produces `out/mac-arm64/Release/rexglue`.

**Runtime (iOS).** Then build and install the iOS runtime libraries:

```sh
cmake --preset ios-arm64
cmake --build --preset ios-arm64-release
cmake --install out/build/ios-arm64 --config Release
```

This produces `librexruntime.dylib`, `librexgpu-xenos.dylib` and `libMoltenVK.dylib` in `out/ios-arm64/Release`, plus a CMake package in `out/install/ios-arm64`.

## 2. Add your game files

1. Extract your disc's ISO (for example with [`extract-xiso`](https://github.com/XboxDev/extract-xiso)) and copy **everything** into `app/game/`.
2. Extract Title Update 9 (for example with Velocity), and copy its **`default.xexp`** into `app/game/` next to `default.xex`. Codegen applies the update at build time.

```
app/game/
├── default.xex
├── default.xexp
├── res/
├── Tutorial/
└── …
```

Keep the TU's full contents (`default.xexp` + `res/`) as well. You'll copy them to the phone in step 5.

## 3. Build the app

```sh
export REXGLUE_SDK=/path/to/rexglue-sdk
cd /path/to/ios-source/app

cmake --preset ios-arm64-release -DMC360_BUNDLE_ID=com.yourname.mcx360
cmake --build --preset ios-arm64-release
```

The first build runs **codegen**, recompiling the game from `game/default.xex` into `generated/default/`. It takes a while and uses a lot of memory. Later builds only recompile what changed.

The app ends up at `app/out/build/ios-arm64-release/mc360.app`.

## 4. Install it on your iPhone

**Easiest: sideload an IPA.**

```sh
./scripts/make-ipa.sh          # writes MCX360Edition.ipa
```

Then install `MCX360Edition.ipa` with AltStore, SideStore, Sideloadly or TrollStore. The script strips all signatures and profiles, so the tool signs it with your own Apple ID.

**For developers:** sign with your own provisioning profile and install over USB.

```sh
PROFILE=~/path/to/your.mobileprovision ./scripts/sign-and-install.sh
```

The profile must cover the bundle ID you configured in step 3.

## 5. Copy the game data to the phone

1. Launch the app once and close it, so it creates its folders.
2. In Finder (select your iPhone → **Files** tab) or the iOS **Files** app, open **MCX360 Edition** and set it up like this:
   ```
   MCX360 Edition/
   ├── game/       ← the contents of app/game/ from step 2
   ├── tu/         ← the full Title Update 9 contents (default.xexp + res/)
   └── mc360.toml  ← settings, created automatically
   ```
3. Pair a controller and launch.

---

## What the SDK patch changes

The patch contains only functional changes, with no debugging instrumentation.

**Build and platform support**
- iOS and MoltenVK build support (CMake presets, cross-compiling with a host `rexglue` tool, MoltenVK embedded in the app bundle).
- iOS-safe memory mapping: a file-backed arena instead of `shm_open`, and `vm_region_64` queries.
- GPU plugin lookup in the bundle's `Frameworks/` folder.
- A log directory fallback, because the signed app bundle is read-only.
- `rexglue dump` for extracting STFS packages such as title updates.

**Rendering fixes**
- **Metal shader miscompile.** SPIRV-Cross turns `NoContraction` operations into `[[clang::optnone]] fma()` helper calls, and Metal miscompiled the terrain shader (world Y came out constant, flattening the world into slivers). Controlled by `shader_no_contraction`, off by default on Apple platforms.
- **Stale vertex buffers.** The residency check was keyed on the fetch slot only, so every terrain chunk after the first in a frame skipped its upload.
- **Primitive restart on Metal.** Metal can't disable primitive restart, so 16-bit index buffers that use `0xFFFF` as a real index are widened to 32-bit (`host_primitive_restart_always_enabled`).
- **MSAA coverage on MoltenVK.** Coverage is derived from `gl_SampleID`, because `gl_SampleMaskIn` never reports sample 0 on iOS (`fsi_sample_mask_from_sample_id`).
- **Built-in index buffer flush.** It flushed the wrong memory, leaving the quad-list and triangle-fan index buffer unflushed.

**Kernel and UI**
- **`RtlUnwind`** is implemented instead of being a stub that returned with a corrupted guest stack.
- **Access-violation logging** reports each distinct address once instead of flooding the log.
- **Achievements screen.** `XamShowAchievementsUI` shows unlocked and locked achievements with their icons.
- **Controller navigation** in all emulator dialogs: A to confirm, B to cancel, D-pad or stick to move.

Performance defaults are set in `app/src/mc360_app.h`:
- `clear_memory_page_state = false`. When on, it forced every uploaded page to be re-copied to the GPU each frame.
- One GPU submission per frame.
- Vsync-locked FIFO presentation.

## Troubleshooting

| Problem | Fix |
|---|---|
| `game/default.xex not found` | Copy your disc files into `app/game/` (step 2). |
| `MC360_IOS_SDK_LIB_DIR ... does not contain librexruntime.dylib` | `REXGLUE_SDK` isn't set, or the iOS SDK build/install (step 1) didn't finish. |
| `This SDK install provides no rexglue CLI` | Build the macOS host tool (step 1) before configuring the app. |
| Codegen errors or crashes at launch | Your disc or TU doesn't match the required version above. |
| Broken textures/fonts in game | `tu/` on the phone is missing or misplaced (step 5). |
| App won't launch after 7 days | A free Apple ID signature expired; re-sign or reinstall. |

On the phone, logs are written to `MCX360 Edition/userdata/logs/`. Please attach the newest one to bug reports.

---

## Credits

- **[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)**: Xbox 360 static recompilation
- **[Xenia](https://xenia.jp)**: the GPU and kernel emulation ReXGlue builds on
- **[MoltenVK](https://github.com/KhronosGroup/MoltenVK)**: Vulkan on Metal

Minecraft is a trademark of Mojang Synergies AB. This project is not affiliated with or endorsed by Mojang, Microsoft, or 4J Studios.
