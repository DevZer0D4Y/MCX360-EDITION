# MCX360 Edition - ReXGlue recompilation of Minecraft: Xbox 360 Edition (TU9), iOS build.
#
# SDK boilerplate lives in generated/rexglue.cmake. The recompiled game code is
# written to generated/default/ by codegen at build time, from your own
# game/default.xex - it is never part of this source tree.

cmake_minimum_required(VERSION 3.25)
project(mc360 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/game/default.xex")
    message(FATAL_ERROR
        "game/default.xex not found.\n"
        "Copy the files extracted from your own Minecraft: Xbox 360 Edition disc into app/game/, "
        "and the Title Update 9 default.xexp next to it. See README.md.")
endif()

include(generated/rexglue.cmake)

set(MC360_SOURCES
    src/main.cpp
)

add_executable(mc360 ${MC360_SOURCES})

# mc360_register.cpp is a flat table of ~54k function-pointer registrations with
# no control flow worth optimising, but -O3 spends >10 minutes on it. -O1
# produces the same table in seconds.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/default/mc360_register.cpp
    PROPERTIES COMPILE_OPTIONS "-O1")

rexglue_setup_target(mc360)

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
