// mc360 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#define MC360_IOS 1
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <rex/cvar.h>
#include <rex/cvar_defaults.h>
#include <rex/logging.h>
#else
#define MC360_IOS 0
#endif

class Mc360App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Mc360App>(new Mc360App(ctx, "mc360",
        PPCImageConfig));
  }

#if MC360_IOS
  // iOS launches an app with no argv, so --game_data_root/--gpu_plugin can never
  // arrive on a command line the way they do on desktop. Resolve them from the
  // app container instead: <container>/Documents is the directory the Files app
  // exposes (UIFileSharingEnabled + LSSupportsOpeningDocumentsInPlace in
  // Info.plist), so that is where the extracted game is dropped.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    const char* home = std::getenv("HOME");
    if (!home || !home[0]) {
      return;
    }
    const std::filesystem::path documents = std::filesystem::path(home) / "Documents";

    std::error_code ec;
    std::filesystem::create_directories(documents, ec);

    if (paths.game_data_root.empty()) {
      paths.game_data_root = documents / "game";
      std::filesystem::create_directories(paths.game_data_root, ec);
    }
    // Always override: ReXApp has already defaulted this to GetUserFolder()/mc360,
    // i.e. $HOME/.local/share/mc360, which is inside the container but invisible
    // to the Files app -- and on device it failed to open for writing at all.
    paths.user_data_root = documents / "userdata";
    std::filesystem::create_directories(paths.user_data_root, ec);

    // Same reason: ReXApp derives cache_root from the pre-override user dir, so
    // it still pointed at $HOME/.local/share/mc360/cache, which the sandbox
    // refuses to create. Everything writable has to live under Documents.
    paths.cache_root = documents / "cache";
    std::filesystem::create_directories(paths.cache_root, ec);
    // The title update ships its own replacement assets (terrain, items, fonts,
    // audio). Without it mounted as update:\ the game fails to open
    // UPDATE:\res\... and falls back to degraded content.
    if (paths.update_data_root.empty()) {
      const std::filesystem::path tu = documents / "tu";
      if (std::filesystem::exists(tu)) {
        paths.update_data_root = tu;
      }
    }

    // Keep the editable config next to the game data rather than inside the
    // (read-only, signed) bundle, so settings can be changed without a rebuild.
    paths.config_path = documents / "mc360.toml";

    if (!std::filesystem::exists(paths.config_path)) {
      std::ofstream out(paths.config_path);
      if (out) {
        out << "# mc360 settings. Edit via the Files app; takes effect on relaunch.\n"
               "# The Xenos GPU emulation renders through Vulkan (MoltenVK on iOS);\n"
               "# leaving this empty disables graphics entirely.\n"
               "gpu_plugin = \"xenos\"\n"
               "\n"
               "# Bit 0 = title is purchased rather than a trial.\n"
               "license_mask = 1\n"
               "\n"
               "# The guest output is 16:9 and the panel is ~19.5:9.\n"
               "#   present_letterbox = false -> stretch to fill (no bars, slight distortion)\n"
               "#   present_letterbox = true  -> keep aspect, pillars down the sides\n"
               "# To fill without distorting, keep letterbox on and crop instead:\n"
               "#   present_allow_overscan_cutoff = true\n"
               "present_letterbox = false\n"
               "\n"
               "# Render target path: \"fbo\" uses host render targets (hardware MSAA\n"
               "# coverage and blending). \"fsi\" emulates the EDRAM in the pixel shader -\n"
               "# more accurate on paper, but much slower here, and MoltenVK does not\n"
               "# report sample 0 coverage, which corrupts every other pixel.\n"
               "render_target_path_vulkan = \"fbo\"\n"
               "\n"
               "# Frame rate cap. 60 keeps frames evenly paced; 0 removes the cap (frames\n"
               "# then follow the screen, and pacing gets uneven when the game cannot\n"
               "# keep up with a 120 Hz iPad).\n"
               "framerate_limit = 60\n"
               "\n"
               "# On-screen gamepad: \"auto\" shows it while no controller is connected,\n"
               "# \"on\" always, \"off\" never. touch_controls_opacity (0.1 to 1.0) dims it.\n"
               "touch_controls = \"auto\"\n";
      }
    }

    // This app's defaults, in place of the SDK's. SetDefaultByName ranks below
    // the config file, so mc360.toml can still change every one of them, and
    // it reaches settings owned by the GPU plugin, which only loads after this
    // hook - SetFlagByName rejects those, which silently dropped the
    // performance defaults below whenever mc360.toml did not repeat them.
    //
    // The Xenos GPU emulation; empty would disable graphics entirely.
    rex::cvar::SetDefaultByName("gpu_plugin", "xenos");
    // XamContentGetLicenseMask returns this to the title. Bit 0 means "purchased";
    // left at 0 an XBLA title such as this one runs as a trial.
    rex::cvar::SetDefaultByName("license_mask", "1");
    // The guest renders 16:9; an iPhone panel is ~19.5:9, so the default
    // letterbox leaves pillars down both sides. Fill the panel instead.
    rex::cvar::SetDefaultByName("present_letterbox", "false");
    // Metal has no fragment-input coverage mask, so MoltenVK's gl_SampleMaskIn
    // never reports sample 0 on this device. The game renders 640x360 with 4x
    // MSAA and resolves the sample grid as the 1280x720 image, so a dropped
    // sample 0 leaves every even pixel of every even row holding the previous
    // frame. Derive coverage from gl_SampleID instead.
    rex::cvar::SetDefaultByName("fsi_sample_mask_from_sample_id", "true");
    // The fragment shader interlock path emulates the EDRAM inside the pixel
    // shader, which is both slow here and subject to the sample-coverage
    // problem above. Host render targets let the hardware do coverage and ROP.
    rex::cvar::SetDefaultByName("render_target_path_vulkan", "fbo");
    // Performance.
    //
    // clear_memory_page_state marks every uploaded page stale at the end of each
    // frame, so all loaded terrain was re-copied to the GPU every frame
    // (~250 MB/s against ~15 MB/s of real guest writes), and the CPU spent half
    // its time waiting for those copies. Upstream Xenia defaults it off; it only
    // matters for titles that depend on GPU-written memory.
    rex::cvar::SetDefaultByName("clear_memory_page_state", "false");
    // Ending the submission at every PM4 primary buffer end cost a vkQueueSubmit
    // plus fence per buffer; once per frame is enough here.
    rex::cvar::SetDefaultByName("vulkan_submit_on_primary_buffer_end", "false");
    // FIFO only: vsync-locked, evenly paced frames capped at the panel refresh.
    // Immediate/mailbox present as soon as a frame is ready, which reads as
    // uneven frame pacing.
    rex::cvar::SetDefaultByName("vulkan_allow_present_mode_immediate", "false");
    rex::cvar::SetDefaultByName("vulkan_allow_present_mode_mailbox", "false");
    rex::cvar::SetDefaultByName("vulkan_allow_present_mode_fifo_relaxed", "false");
    // A steady 60. iPhones already present at up to 60 Hz, but iPads with
    // ProMotion allow 120, and letting frames through as fast as the game makes
    // them (anywhere between 30 and 120) paces them unevenly - tested worse
    // than a 60 cap.
    rex::cvar::SetDefaultByName("framerate_limit", "60");
    // Save thumbnails: the game renders the picture on the GPU and encodes the
    // 64x64 result on the CPU, which never saw it. Small resolves like that are
    // copied back to memory right away; the per-frame 1280x720 one isn't.
    rex::cvar::SetDefaultByName("readback_resolve_small_kb", "256");
    // Background shader compiling skips any draw whose pipeline isn't ready.
    // Normal frames hide that, but the thumbnail passes run once per save, so
    // they were always skipped and the picture came out as one flat colour.
    rex::cvar::SetDefaultByName("async_shader_compilation", "false");
    // Work out how far a screen-space draw (the game's clear rectangles, drawn
    // with Direct3D's unbounded 8192x8192 scissor) really reaches by running its
    // vertex shader on the CPU, as Xenia does by default. Without it the
    // depth-only clears were assumed to cover the whole EDRAM, wrapping over the
    // color image: every frame paid for moving the image out and back, and a
    // save's thumbnail, grabbed right after such a clear, came out black.
    rex::cvar::SetDefaultByName("execute_unclipped_draw_vs_on_cpu", "true");
    // 16 queued audio frames (~85 ms) ride out scheduling hiccups on a busy or
    // warm phone; 8 ran dry, heard as choppy sound.
    rex::cvar::SetDefaultByName("audio_maxqframes", "16");

    REXLOG_INFO("iOS paths: game={} update={} config={}", paths.game_data_root.string(),
                paths.update_data_root.string(), paths.config_path.string());
  }
#endif
};
