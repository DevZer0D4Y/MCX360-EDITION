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
               "render_target_path_vulkan = \"fbo\"\n";
      }
    }

    // The config file above is loaded immediately after this hook returns, but
    // only if it parsed; force the default so a malformed or deleted file still
    // leaves a usable GPU backend.
    if (rex::cvar::GetFlagByName("gpu_plugin").empty()) {
      rex::cvar::SetFlagByName("gpu_plugin", "xenos");
    }
    // XamContentGetLicenseMask returns this to the title. Bit 0 means "purchased";
    // left at 0 an XBLA title such as this one runs as a trial.
    if (rex::cvar::GetFlagByName("license_mask") == "0") {
      rex::cvar::SetFlagByName("license_mask", "1");
    }
    // The guest renders 16:9; an iPhone panel is ~19.5:9, so the default
    // letterbox leaves pillars down both sides. Fill the panel instead.
    rex::cvar::SetFlagByName("present_letterbox", "false");
    // Metal has no fragment-input coverage mask, so MoltenVK's gl_SampleMaskIn
    // never reports sample 0 on this device. The game renders 640x360 with 4x
    // MSAA and resolves the sample grid as the 1280x720 image, so a dropped
    // sample 0 leaves every even pixel of every even row holding the previous
    // frame. Derive coverage from gl_SampleID instead.
    rex::cvar::SetFlagByName("fsi_sample_mask_from_sample_id", "true");
    // The fragment shader interlock path emulates the EDRAM inside the pixel
    // shader, which is both slow here and subject to the sample-coverage
    // problem above (the guest renders 640x360 at 4x MSAA and resolves the
    // sample grid as the 1280x720 image, so a dropped sample corrupts every
    // other pixel). Host render targets let the hardware do coverage and ROP.
    if (rex::cvar::GetFlagByName("render_target_path_vulkan").empty()) {
      rex::cvar::SetFlagByName("render_target_path_vulkan", "fbo");
    }
    // Performance defaults, set before the config file loads so it can still
    // override them.
    //
    // clear_memory_page_state marks every uploaded page stale at the end of each
    // frame, so all loaded terrain was re-copied to the GPU every frame
    // (~250 MB/s against ~15 MB/s of real guest writes), and the CPU spent half
    // its time waiting for those copies. Upstream Xenia defaults it off; it only
    // matters for titles that depend on GPU-written memory.
    rex::cvar::SetFlagByName("clear_memory_page_state", "false");
    // Ending the submission at every PM4 primary buffer end cost a vkQueueSubmit
    // plus fence per buffer; once per frame is enough here.
    rex::cvar::SetFlagByName("vulkan_submit_on_primary_buffer_end", "false");
    // FIFO only: vsync-locked, evenly paced frames capped at the panel refresh.
    // Immediate/mailbox present as soon as a frame is ready, which reads as
    // uneven frame pacing.
    rex::cvar::SetFlagByName("vulkan_allow_present_mode_immediate", "false");
    rex::cvar::SetFlagByName("vulkan_allow_present_mode_mailbox", "false");
    rex::cvar::SetFlagByName("vulkan_allow_present_mode_fifo_relaxed", "false");

    REXLOG_INFO("iOS paths: game={} update={} config={}", paths.game_data_root.string(),
                paths.update_data_root.string(), paths.config_path.string());
  }
#endif
};
