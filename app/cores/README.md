# Title update cores

Each folder here is one title update (TU) the app can run. At build time its
recompiled code becomes its own library, `libmc360core_<folder>.dylib`, inside
the app's `Frameworks/` folder. The app's launcher menu lists every core that
was built, and starts the one the player picks.

```
cores/
├── tu9/
│   ├── core.cmake      name shown in the menu, and the player's folder names
│   ├── manifest.toml   codegen manifest: which files to recompile, and fixes
│   └── hooks.cpp       host replacements for guest functions (optional)
└── tu14/               (example: another title update)
    ├── core.cmake
    ├── manifest.toml
    ├── hooks.cpp
    └── game/           build-time copy of the files codegen reads
        ├── default.xex   from your disc (same file as app/game/default.xex)
        └── default.xexp  from that title update
```

The game version, Title ID and Media ID a core expects are read from the
`default.xex` and `default.xexp` codegen uses, so they always match the
recompiled code. On the phone, the launcher compares them with the player's
files and explains what is wrong before anything starts.

## Why each title update needs its own core

ReXGlue recompiles the game's PowerPC code ahead of time. Every title update
ships a different `default.xex`, so the recompiled code, every function address
and every fix in `manifest.toml` are specific to one title update. TU9's
manifest is the result of running the game, finding where it trapped, and
declaring what codegen missed. Expect to repeat that process for each new
title update.

## Adding a title update

You need your own copy of the disc and of that title update, and an iPhone (or
a desktop build) to test on.

1. **Make the folder.** Copy `tu9/` to `tu<N>/`. In `core.cmake`, set
   `CORE_NAME` and **delete** the three `CORE_*_DIR` lines (they keep TU9 on
   its original folders). New cores then use `tu<N>/`, `userdata-tu<N>/` and
   `cache-tu<N>/` on the phone.
2. **Point the manifest at that title update's files.** Create
   `tu<N>/game/` holding your disc's `default.xex` and the title update's
   `default.xexp`. In `manifest.toml`, set:
   ```toml
   game_root = "game"
   file_path = "game/default.xex"
   out_directory_path = "generated"
   ```
   Keep `[project] name = "mc360"`.
3. **Remove TU9's addresses.** Everything under `[entrypoint.functions]`,
   `setjmp_address` and `longjmp_address` is a TU9 address. Delete them, then
   add back what this title update needs:
   - `setjmp_address` / `longjmp_address`: find the CRT's `setjmp`/`longjmp`
     in this executable. Without them, the image loader's JPEG probe corrupts
     the stack (see the comment in `tu9/manifest.toml`).
   - Function entries: run the game, and declare each address the runtime
     traps on as an unregistered function (the log names it). Codegen's
     `--scan_code_pointers_to` option finds vtable and callback targets in
     bulk; see the comments in `tu9/manifest.toml` for each kind of fix.
4. **Port the performance hooks.** `tu9/hooks.cpp` replaces two functions
   that matter for frame rate and heat on a phone. Find the same functions in
   this title update and use their addresses:
   - The PIX named-event function, which formats event text on every call
     and then drops it. TU9 checks a callback pointer at `0x82000A04`.
   - Direct3D's GPU spin-wait step, which spins a core at full power waiting
     for the GPU. TU9 sleeps 50 µs before each check.

   Without them the title update still runs, but slower and hotter.
5. **Build.** The next CMake configure runs codegen for the new core and
   prints the version it read, for example
   `Core 'tu14' (Title Update 14): Title ID 584111F7, Media ID 7CD33B56, version 0.0.17.1`.
   `MC360_CORES` picks which cores go into a build and their menu order
   (default: every folder here, in natural order).
6. **Test on the phone** until it plays like TU9 does: menus, creating and
   loading worlds, saving (the thumbnail too), and a long play session for
   frame pacing and heat.

A core whose `default.xex` is missing is skipped with a warning, so folders
for title updates you don't have the files for don't stop the build.

## Size

Each core is roughly the size of the whole single-TU app, so every title update
added makes the IPA bigger by about that much.
