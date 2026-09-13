# MCX360 Edition: Minecraft: Xbox 360 Edition for iPhone

A native iOS port of **Minecraft: Xbox 360 Edition (Title Update 9)**, built with the [ReXGlue](https://github.com/rexglue/rexglue-sdk) static recompilation SDK. The Xbox 360 code is recompiled ahead of time to run natively on iPhone. Graphics run through Vulkan on Metal via MoltenVK. **Developer mode MIGHT be required on older and newer iOS, enable it going trough: Settings > Privacy & Security, scroll to the bottom, toggle on Developer Mode, and restart your device.**

> [!IMPORTANT]
> **You need your own copy of the game.** This release does not include the game's data files (textures, sounds, worlds, the original `default.xex`). You provide them from a disc and title update you own. This project is not affiliated with Mojang, Microsoft, or 4J Studios.

---

## Requirements

| | |
|---|---|
| **Device** | iPhone, iOS 16 or later |
| **Controller** | A Bluetooth controller: Xbox, PlayStation, or MFi. **There are no touch controls.** |
| **Computer** | Needed once, to sideload the app and extract the game files |
| **Game files** | Minecraft: Xbox 360 Edition, **disc version** |
| **Title update** | **TU9** for that disc |

Tested so far on a single 2796×1290 iPhone. Other devices should work, but they're unconfirmed.

### Your files must match exactly

The app was recompiled from one specific build of the game, so it only runs with that build:

| Field | Required value |
|---|---|
| Title ID | `584111F7` |
| Media ID | `7CD33B56` |
| Version after TU | `0.0.10.1` (TU9) |

A different disc revision or title update will not work.

---

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
- **[Sideloadly](https://sideloadly.io)**
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

**Achievements:** open the pause menu and select **Achievements** to see what you've unlocked and what's still locked. Achievement progress is saved on the phone.

---

## Settings

`mc360.toml` sits next to your game folders and can be edited in the Files app. Changes take effect the next time you launch the app.

| Setting | Default | What it does |
|---|---|---|
| `present_letterbox` | `false` | `false` stretches the 16:9 image to fill the screen; `true` keeps the aspect ratio with black bars on the sides |

The performance settings are already tuned and built into the app. You don't need to add anything.

---

## Troubleshooting

**Black screen, or the app closes right away**
- Check that `game/default.xex` sits directly inside `game/`.
- Check that your disc matches the Title ID and Media ID above.

**Textures, items or fonts look wrong**
- The title update is missing or in the wrong place. Check that `tu/default.xexp` exists.

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

- **ReXGlue SDK**: Xbox 360 static recompilation toolkit
- **[Xenia](https://xenia.jp)**: the Xbox 360 GPU and kernel emulation work ReXGlue builds on
- **[MoltenVK](https://github.com/KhronosGroup/MoltenVK)**: Vulkan on Metal

Minecraft is a trademark of Mojang Synergies AB. This project is not affiliated with or endorsed by Mojang, Microsoft, or 4J Studios.

## A special thank you
A special thank you goes to BlindEye Studios, the team where I work, thank you for supporting me trough this project, I really love you all!! This project has been released under BlindEye Studios, and made by the one and only DevZ.
