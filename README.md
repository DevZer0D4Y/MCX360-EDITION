# MCX360 Edition: Minecraft: Xbox 360 Edition for iPhone

A native iOS port of **Minecraft: Xbox 360 Edition (Title Update 9)**, built with the [ReXGlue](https://github.com/rexglue/rexglue-sdk) static recompilation SDK. The Xbox 360 code is recompiled ahead of time to run natively on iPhone. Graphics run through Vulkan on Metal via MoltenVK. **Developer mode MIGHT be required on older and newer iOS, enable it going trough: Settings > Privacy & Security, scroll to the bottom, toggle on Developer Mode, and restart your device.**

> [!IMPORTANT]
> **You need your own copy of the game.** This release does not include the game's data files (textures, sounds, worlds, the original `default.xex`). You provide them from a disc and title update you own. This project is not affiliated with Mojang, Microsoft, or 4J Studios.

If you want to support me and remain up-to-date about this and other projects, you can in different ways:

Ko-Fi: https://ko-fi.com/dev_zer0

Discord: https://discord.gg/uFChheZEWX

YouTube: https://www.youtube.com/@develop_erZ

---

## Requirements

| | |
|---|---|
| **Device** | iPhone, iOS 16 or later |
| **Controller** | A Bluetooth controller (recommended): Xbox, PlayStation, or MFi. **There are touch controls.** |
| **Computer** | Needed once, to sideload the app and extract the game files |
| **Game files** | Minecraft: Xbox 360 Edition, **disc version** |
| **Title update** | **TU9** for that disc |

### Your files must match exactly

The app was recompiled from one specific build of the game, so it only runs with that build:

| Field | Required value |
|---|---|
| Title ID | `584111F7` |
| Media ID | `7CD33B56` |
| Version after TU | `0.0.10.1` (TU9) |

A different disc revision or title update will not work.

---

> [!TIP]
> **No computer needed for unpacking.** If you already have your disc image (`.iso`) and the TU9 package as files, skip Steps 1, 2 and 4: install the app (Step 3), open it once, then put both files into the **MCX360 Edition** folder in the Files app (from iCloud Drive, a USB drive, AirDrop, a download…). Open the app again and tap **Import** next to each one. It unpacks them into the right folders and checks they're the right versions. You can delete the originals afterwards.
>
> Unpacking needs free space for the unpacked files on top of the image (the app checks before it starts and tells you how much). Keep the app open until it finishes.

## Step 1: Extract the game files

1. Make an ISO of your disc with the dumping method you normally use.
2. Extract the ISO's contents with a tool such as [`extract-xiso`](https://github.com/XboxDev/extract-xiso):
   ```sh
   extract-xiso -x Minecraft.iso -d game
   ```
3. You should end up with a `game` folder containing, among other things:
   ```
   game/
   ├── default.xex        (about 20 MB)
   ├── ArcadeInfo.xml
   ├── nuisp1031 … nuisp3084
   ├── res/
   └── Tutorial/
   ```

Keep the whole folder. Don't pick out individual files.

## Step 2: Extract Title Update 9

TU9 comes as an Xbox 360 content package. Open it with a package tool such as **Velocity** and extract its contents. You should get:

```
tu/
├── default.xexp
└── res/
```

The title update is required. It supplies the game's updated textures, items, fonts and audio, and without it the game falls back to broken content.

## Step 3: Install the app

Download **`MCX360Edition.ipa`** from the Releases page and sideload it with any of these:

- **[AltStore](https://altstore.io) / [SideStore](https://sidestore.io)**
- **[Sideloadly](https://sideloadly.io)** (enable File Sharing trough Advanced Options)
- **TrollStore**, if your iOS version supports it

The IPA is unsigned; your sideloading tool signs it with your own Apple ID.

> [!NOTE]
> With a free Apple ID, sideloaded apps stop launching after **7 days**. Refresh it in AltStore/SideStore, or reinstall. Your worlds and settings stay on the phone.

## Step 4: Copy the game onto your phone

1. **Launch the app once** and close it. This creates its folders.
2. Connect the iPhone to your computer. On a Mac, select it in Finder and open the **Files** tab; on Windows, use iTunes' File Sharing.
   *Alternatively:* copy the folders onto the iPhone some other way (iCloud Drive, AirDrop), then use the **Files** app to move them.
3. Open the **MCX360 Edition** folder under *On My iPhone* and arrange it exactly like this:
   ```
   MCX360 Edition/
   ├── game/          ← everything from Step 1
   │   ├── default.xex
   │   ├── res/
   │   └── …
   ├── tu/            ← everything from Step 2 (create this folder)
   │   ├── default.xexp
   │   └── res/
   └── mc360.toml     ← settings (created automatically)
   ```

`game/` must contain `default.xex` directly, not inside another subfolder. The same applies to `default.xexp` in `tu/`.

## Step 5: Play

1. Pair your controller in **Settings → Bluetooth**.
2. Launch **MCX360 Edition**.

The first launch can take a little longer while the game loads.

With one title update and one instance, the app goes straight into the game, as before. Otherwise it opens a menu first (see below).

**Achievements:** open the pause menu and select **Achievements** to see what you've unlocked and what's still locked. Achievement progress is saved on the phone.

---

## Title updates and instances

The app is built as a launcher: each title update it supports is a separate "core" inside the app, and a menu at startup lets you pick one. Right now the app ships with **TU9** only. Other title updates have to be recompiled and tested one by one before they can be added (see [`app/cores/README.md`](app/cores/README.md)), and they will show up in the menu as they are. Importing a title update's files on the phone sets up its files; it doesn't make a title update playable that the app wasn't built with.

**The menu** opens when there is something to choose, or when something is wrong with your files:
- Tap an **Instance** to play it, or use the D-pad and **A**. The one you played last is selected, so with a controller you can just press **A**.
- **+ New instance** makes a separate set of worlds, settings and achievements for that title update. Instance 1 uses `userdata/`, instance 2 uses `userdata-2/`, and so on. Delete an instance by deleting its folder in the Files app.
- If a title update's files are missing or don't match, it says what's wrong and where the files go. **Start anyway** is there if you want to try regardless.

To switch title update or instance, close the app and open it again.

**Folders.** TU9 keeps the folders shown in Step 4, so nothing changes for existing installs. Each other title update gets its own:

```
MCX360 Edition/
├── game/            ← your disc files, shared by every title update
├── tu/              ← Title Update 9
├── tu14/            ← e.g. Title Update 14, when a build includes it
├── userdata/        ← TU9 worlds and settings (instance 1)
├── userdata-2/      ← TU9, instance 2
├── userdata-tu14/   ← TU14 worlds and settings
└── mc360.toml
```

The launcher reads each title update's version from its `default.xexp`, so a title update in a folder with a different name is still found. Keep `default.xexp` out of `game/`: the game applies one found there whichever title update you start.

**Importing on the phone.** A disc image (`.iso`) or title update package dropped into the app's folder (or an `import/` folder inside it) shows up at the top of the menu under **Files to set up**, with an **Import** button. Title update packages are matched to the right title update by their version. A package for a title update this copy of the app can't play yet is still unpacked, into `tu-<version>/`, ready for a later version of the app.

---

## Settings

`mc360.toml` sits next to your game folders and can be edited in the Files app. Changes take effect the next time you launch the app.

| Setting | Default | What it does |
|---|---|---|
| `present_letterbox` | `false` | `false` stretches the 16:9 image to fill the screen; `true` keeps the aspect ratio with black bars on the sides |
| `launcher_menu` | `"auto"` | `"auto"` shows the title update menu only when there is a choice or a problem; `"always"` shows it at every launch (for example, to make a second instance) |

The performance settings are already tuned and built into the app. You don't need to add anything.

---

## Building from source (Mac)

With the patched ReXGlue SDK built for macOS and iOS (`sdk/rexglue-sdk-ios.patch`) and your game files in `app/game/`:

```bash
REXGLUE_SDK=/path/to/rexglue-sdk scripts/build-ipa.sh
```

That runs the launcher's tests (`app/tests/`, a few seconds), then builds every title update core whose files are present, the launcher, and `MCX360Edition.ipa`. To start a new title update core from its package:

```bash
REXGLUE_SDK=/path/to/rexglue-sdk scripts/new-core.sh tu14 path/to/TU_package
```

Then follow [`app/cores/README.md`](app/cores/README.md) to get it running.

---

## Troubleshooting

**Black screen, or the app closes right away**
- Check that `game/default.xex` sits directly inside `game/`.
- Check that your disc matches the Title ID and Media ID above.

**Textures, items or fonts look wrong**
- The title update is missing or in the wrong place. Check that `tu/default.xexp` exists.

**The menu shows "Check files" or "Can't start"**
- The line under the title update says what's wrong. Most often the title update's folder is missing, it holds a different title update, or `game/` is from another disc version.

**Stuck on the loading screen when opening a world**
- Force-close the app and launch it again.
- If it keeps happening, open an issue and attach the newest log from `userdata/logs/` (see below).

**App won't open after a week**
- The free signing period expired. Refresh or reinstall it with your sideloading tool (Step 3).

### Reporting a bug

Logs are saved to `MCX360 Edition/userdata/logs/` in the Files app. When you open an issue, please include:
- the newest `mc360_*.log` file
- your iPhone model and iOS version
- what you were doing when it happened

---

## Checksum

```
SHA-256  9d17eedf77bb200b97ca2dc3931c9096eb5063f14ce2d3935bb59fa5e4150596  MCX360Edition.ipa
```

## Credits

- **[ReXGlue](https://github.com/rexglue/rexglue-sdk)**: Xbox 360 static recompilation toolkit
- **[Xenia](https://xenia.jp)**: the Xbox 360 GPU and kernel emulation work ReXGlue builds on
- **[MoltenVK](https://github.com/KhronosGroup/MoltenVK)**: Vulkan on Metal

Minecraft is a trademark of Mojang Synergies AB. This project is not affiliated with or endorsed by Mojang, Microsoft, or 4J Studios.
