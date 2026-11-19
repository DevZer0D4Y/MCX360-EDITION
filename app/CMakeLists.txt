# MCX360 Edition - ReXGlue recompilation of Minecraft: Xbox 360 Edition, iOS build.
#
# The app is a launcher (the mc360 executable) plus one core library per title
# update: cores/<id>/ holds that title update's codegen manifest and hooks, and
# its recompiled code is generated into cores/<id>/generated/ at build time
# from your own game files - it is never part of this source tree. The
# launcher shows a menu at startup when there is more than one title update
# (or instance) to choose from, loads the chosen core and runs it.
#
# SDK boilerplate lives in generated/rexglue.cmake, the per-core build in
# cmake/mc360_cores.cmake.

cmake_minimum_required(VERSION 3.25)
project(mc360 LANGUAGES CXX)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # The launcher reads controllers through the GameController framework.
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 23)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

set(MC360_CORES "" CACHE STRING
    "Title update cores to build, in menu order (e.g. \"tu9;tu14\"). Empty builds every folder in cores/.")

# Keep build-machine paths, which usually contain your user name, out of the
# binaries: source file names end up in log and assert messages.
add_compile_options("-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=mcx360")
if(DEFINED ENV{REXGLUE_SDK})
    add_compile_options("-ffile-prefix-map=$ENV{REXGLUE_SDK}=rexglue-sdk")
endif()

include(generated/rexglue.cmake)
include(cmake/mc360_cores.cmake)

set(MC360_SOURCES
    src/main.cpp
    src/mc360_app.cpp
    src/launcher/core_loader.cpp
    src/launcher/launcher.cpp
    src/launcher/launcher_dialog.cpp
    src/launcher/xex_info.cpp
)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    list(APPEND MC360_SOURCES src/launcher/gamepad_poll_ios.mm)
else()
    list(APPEND MC360_SOURCES src/launcher/gamepad_poll_stub.cpp)
endif()

add_executable(mc360 ${MC360_SOURCES})

rexglue_setup_target(mc360)

# One shared library per title update, embedded next to the runtime dylibs,
# plus the launcher's table of them.
mc360_add_cores(mc360)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(MC360_BUNDLE_ID "com.example.mcx360" CACHE STRING
        "iOS bundle identifier. Use one your signing identity can provision.")
    set(MC360_IOS_SDK_LIB_DIR "" CACHE PATH
        "Directory with the iOS SDK runtime dylibs (rexglue-sdk/out/ios-arm64/Release)")
    if(NOT EXISTS "${MC360_IOS_SDK_LIB_DIR}/librexruntime.dylib")
        message(FATAL_ERROR
            "MC360_IOS_SDK_LIB_DIR='${MC360_IOS_SDK_LIB_DIR}' does not contain librexruntime.dylib.\n"
            "Build the patched SDK for iOS first and set REXGLUE_SDK (see README.md).")
    endif()

    # Produce a .app rather than a bare executable.
    set_target_properties(mc360 PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/platform/ios/Info.plist"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${MC360_BUNDLE_ID}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${MC360_BUNDLE_ID}"
        # The SDK's helper sets @executable_path, which is right for a flat macOS
        # layout; inside a bundle the dylibs live in Frameworks/.
        INSTALL_RPATH "@executable_path/Frameworks"
        BUILD_WITH_INSTALL_RPATH ON
    )
    target_link_libraries(mc360 PRIVATE "-framework GameController")

    # Everything the app dlopens or links must ship inside the bundle and be
    # signed with it: iOS will not load a dylib from outside the app.
    set(MC360_IOS_EMBED
        "${MC360_IOS_SDK_LIB_DIR}/librexruntime.dylib"
        "${MC360_IOS_SDK_LIB_DIR}/librexgpu-xenos.dylib"
        "${MC360_IOS_SDK_LIB_DIR}/libMoltenVK.dylib"
    )
    add_custom_command(TARGET mc360 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:mc360>/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${MC360_IOS_EMBED}
            "$<TARGET_BUNDLE_CONTENT_DIR:mc360>/Frameworks"
        COMMENT "Embedding runtime dylibs into mc360.app/Frameworks"
        VERBATIM
    )

    # Optional app icons (not included in this repo). Without an asset catalog
    # iOS resolves icons by filename from the bundle root, driven by
    # CFBundleIcons in Info.plist: drop AppIcon60x60@2x.png etc. into
    # platform/ios/icons/ to use your own.
    file(GLOB MC360_IOS_ICONS "${CMAKE_CURRENT_SOURCE_DIR}/platform/ios/icons/*.png")
    if(MC360_IOS_ICONS)
        add_custom_command(TARGET mc360 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${MC360_IOS_ICONS}
                "$<TARGET_BUNDLE_CONTENT_DIR:mc360>"
            COMMENT "Copying app icons into mc360.app"
            VERBATIM
        )
    endif()
endif()
