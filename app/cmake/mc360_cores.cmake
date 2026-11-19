# Title update cores.
#
# Every folder under cores/ that has a core.cmake is one title update. Each is
# recompiled from its own manifest.toml into its own shared library,
# libmc360core_<id>.dylib, which the launcher (the mc360 executable) loads at
# startup. See cores/README.md for adding one.
#
#   MC360_CORES   Which cores to build, in menu order. Defaults to every folder
#                 under cores/ (sorted). A core whose game files are missing is
#                 skipped with a warning.

include_guard(GLOBAL)

set(MC360_CORES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cores")

# -----------------------------------------------------------------------------
# XEX2 header fields, read the same way as src/launcher/xex_info.cpp. Sets
# <prefix>_FOUND, <prefix>_HAS_EXEC, <prefix>_TITLE_ID, <prefix>_MEDIA_ID,
# <prefix>_VERSION (as 8 hex digits), <prefix>_HAS_DELTA and
# <prefix>_TARGET_VERSION in the caller's scope.
# -----------------------------------------------------------------------------
function(_mc360_read_be32 file offset out_var)
    file(READ "${file}" _hex OFFSET ${offset} LIMIT 4 HEX)
    string(LENGTH "${_hex}" _len)
    if(NOT _len EQUAL 8)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    string(TOUPPER "${_hex}" _hex)
    set(${out_var} "${_hex}" PARENT_SCOPE)
endfunction()

function(_mc360_xex_info file prefix)
    set(_found FALSE)
    set(_has_exec FALSE)
    set(_has_delta FALSE)
    set(_title "")
    set(_media "")
    set(_version "")
    set(_target "")
    if(EXISTS "${file}")
        _mc360_read_be32("${file}" 0 _magic)
        if(_magic STREQUAL "58455832")  # 'XEX2'
            set(_found TRUE)
            _mc360_read_be32("${file}" 20 _count_hex)
            set(_count 0)
            if(NOT _count_hex STREQUAL "")
                math(EXPR _count "0x${_count_hex}")
                if(_count GREATER 256)
                    set(_count 0)
                endif()
            endif()
            set(_i 0)
            while(_i LESS _count)
                math(EXPR _entry "24 + ${_i} * 8")
                math(EXPR _value_at "${_entry} + 4")
                _mc360_read_be32("${file}" ${_entry} _key)
                _mc360_read_be32("${file}" ${_value_at} _value)
                if(_key STREQUAL "" OR _value STREQUAL "")
                    # Truncated header; leave the rest unread.
                elseif(_key STREQUAL "00040006")  # execution info
                    math(EXPR _off "0x${_value}")
                    math(EXPR _off_version "${_off} + 4")
                    math(EXPR _off_title "${_off} + 12")
                    _mc360_read_be32("${file}" ${_off} _media)
                    _mc360_read_be32("${file}" ${_off_version} _version)
                    _mc360_read_be32("${file}" ${_off_title} _title)
                    if(NOT _media STREQUAL "" AND NOT _version STREQUAL "" AND NOT _title STREQUAL "")
                        set(_has_exec TRUE)
                    endif()
                elseif(_key STREQUAL "000005FF")  # delta patch descriptor
                    math(EXPR _off "0x${_value}")
                    math(EXPR _off_target "${_off} + 4")
                    _mc360_read_be32("${file}" ${_off_target} _target)
                    if(NOT _target STREQUAL "")
                        set(_has_delta TRUE)
                    endif()
                endif()
                math(EXPR _i "${_i} + 1")
            endwhile()
        endif()
    endif()
    set(${prefix}_FOUND ${_found} PARENT_SCOPE)
    set(${prefix}_HAS_EXEC ${_has_exec} PARENT_SCOPE)
    set(${prefix}_TITLE_ID "${_title}" PARENT_SCOPE)
    set(${prefix}_MEDIA_ID "${_media}" PARENT_SCOPE)
    set(${prefix}_VERSION "${_version}" PARENT_SCOPE)
    set(${prefix}_HAS_DELTA ${_has_delta} PARENT_SCOPE)
    set(${prefix}_TARGET_VERSION "${_target}" PARENT_SCOPE)
endfunction()

# "0000A01" -> "0.0.10.1" (major:4 minor:4 build:16 qfe:8)
function(_mc360_format_version hex out_var)
    math(EXPR _v "0x${hex}")
    math(EXPR _major "(${_v} >> 28) & 15")
    math(EXPR _minor "(${_v} >> 24) & 15")
    math(EXPR _build "(${_v} >> 8) & 65535")
    math(EXPR _qfe "${_v} & 255")
    set(${out_var} "${_major}.${_minor}.${_build}.${_qfe}" PARENT_SCOPE)
endfunction()

function(_mc360_cpp_string value out_var)
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    set(${out_var} "\"${value}\"" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# mc360_add_core(<id> <host_target>)
# -----------------------------------------------------------------------------
function(mc360_add_core core_id host_target)
    set(core_dir "${MC360_CORES_DIR}/${core_id}")
    set(manifest "${core_dir}/manifest.toml")
    if(NOT EXISTS "${core_dir}/core.cmake" OR NOT EXISTS "${manifest}")
        message(FATAL_ERROR "Core '${core_id}': ${core_dir} needs core.cmake and manifest.toml.")
    endif()

    # Per-core settings, with defaults for new cores.
    set(CORE_NAME "${core_id}")
    set(CORE_UPDATE_DIR "${core_id}")
    set(CORE_SAVE_DIR "userdata-${core_id}")
    set(CORE_CACHE_DIR "cache-${core_id}")
    include("${core_dir}/core.cmake")

    # Where codegen reads and writes, from the manifest itself so the two
    # cannot disagree.
    file(READ "${manifest}" _manifest)
    string(REGEX MATCH "\nname[ \t]*=[ \t]*\"([^\"]+)\"" _m "${_manifest}")
    if(NOT CMAKE_MATCH_1 STREQUAL "mc360")
        message(FATAL_ERROR "Core '${core_id}': manifest.toml must keep [project] name = \"mc360\" "
            "(the launcher and hooks include mc360_init.h).")
    endif()
    string(REGEX MATCH "\nfile_path[ \t]*=[ \t]*\"([^\"]+)\"" _m "${_manifest}")
    set(xex "${core_dir}/${CMAKE_MATCH_1}")
    string(REGEX MATCH "\nout_directory_path[ \t]*=[ \t]*\"([^\"]+)\"" _m "${_manifest}")
    set(gen_dir "${core_dir}/${CMAKE_MATCH_1}")
    get_filename_component(xex "${xex}" ABSOLUTE)
    get_filename_component(gen_dir "${gen_dir}" ABSOLUTE)
    set(xexp "${xex}p")

    if(NOT EXISTS "${xex}")
        message(WARNING "Core '${core_id}' (${CORE_NAME}) skipped: ${xex} not found. "
            "See cores/README.md for the files each core needs.")
        return()
    endif()

    # What this core expects to find on the phone. Read from the exact files
    # codegen uses (codegen applies the default.xexp next to default.xex).
    _mc360_xex_info("${xex}" BASE)
    set(identity_known FALSE)
    set(has_update FALSE)
    set(game_version "00000000")
    if(BASE_HAS_EXEC)
        set(identity_known TRUE)
        set(game_version "${BASE_VERSION}")
        if(EXISTS "${xexp}")
            set(has_update TRUE)
            _mc360_xex_info("${xexp}" PATCH)
            if(PATCH_HAS_DELTA)
                set(game_version "${PATCH_TARGET_VERSION}")
            elseif(PATCH_HAS_EXEC)
                set(game_version "${PATCH_VERSION}")
            else()
                set(identity_known FALSE)
            endif()
        endif()
    endif()
    if(identity_known)
        _mc360_format_version("${game_version}" _version_text)
        message(STATUS "Core '${core_id}' (${CORE_NAME}): Title ID ${BASE_TITLE_ID}, "
            "Media ID ${BASE_MEDIA_ID}, version ${_version_text}")
    else()
        message(WARNING "Core '${core_id}': could not read the game version from ${xex} "
            "or its default.xexp; the launcher will not check the player's files for this core.")
        set(BASE_TITLE_ID "00000000")
        set(BASE_MEDIA_ID "00000000")
        if(EXISTS "${xexp}")
            set(has_update TRUE)
        endif()
    endif()

    # First configure: generate now, so the source list below is complete
    # without a second configure. Needs the host rexglue CLI as a file (always
    # the case when cross-compiling for iOS).
    if(NOT EXISTS "${gen_dir}/sources.cmake" AND EXISTS "${REXGLUE_CODEGEN_COMMAND}")
        message(STATUS "Core '${core_id}': running codegen (first configure; this takes a while)")
        execute_process(
            COMMAND "${REXGLUE_CODEGEN_COMMAND}" codegen "${manifest}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "Core '${core_id}': codegen failed (${_rc}).")
        endif()
    endif()

    # Codegen re-runs as part of the build whenever an input in codegen.d
    # changes. The stamp comes first: the DEPFILE names it.
    set(core_sources "")
    if(EXISTS "${gen_dir}/sources.cmake")
        include("${gen_dir}/sources.cmake")
        set(core_sources ${GENERATED_SOURCES})
    endif()
    add_custom_command(
        OUTPUT "${gen_dir}/codegen.build.stamp" ${core_sources}
        COMMAND ${REXGLUE_CODEGEN_COMMAND} codegen "${manifest}"
        DEPFILE "${gen_dir}/codegen.d"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating recompiled code for core ${core_id}"
        VERBATIM)
    add_custom_target(mc360_codegen_${core_id} DEPENDS "${gen_dir}/codegen.build.stamp")

    if(NOT core_sources)
        message(WARNING "Core '${core_id}' skipped this time: its code has not been generated "
            "yet. Build the mc360_codegen_${core_id} target, then configure again.")
        return()
    endif()

    # Compile settings identical to the single-build app's recomp library:
    # same flags on the generated sources, same PCH, and the function table at
    # -O1 (it is a flat list of registrations; -O3 takes >10 minutes on it
    # for the same result).
    set_source_files_properties(${core_sources}
        PROPERTIES COMPILE_OPTIONS "${REXGLUE_RECOMP_OPTIONS}")
    set_source_files_properties("${gen_dir}/mc360_register.cpp"
        PROPERTIES COMPILE_OPTIONS "-O1")

    set(target mc360core_${core_id})
    set(extra_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/core/core_entry.cpp")
    # Host replacements for guest functions must be in the same library as the
    # code they replace (the generated functions are hidden weak symbols).
    if(EXISTS "${core_dir}/hooks.cpp")
        list(APPEND extra_sources "${core_dir}/hooks.cpp")
    endif()
    add_library(${target} SHARED ${core_sources} ${extra_sources})
    target_compile_definitions(${target} PRIVATE MC360_CORE_ID="${core_id}")
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${gen_dir}")
    target_link_libraries(${target} PRIVATE rex::runtime)
    rexglue_apply_target_settings(${target})
    rexglue_apply_recomp_settings(${target} "${gen_dir}/mc360_pch.h")
    add_dependencies(${target} mc360_codegen_${core_id})
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "mc360core_${core_id}"
        # Found next to the runtime dylibs it links (Frameworks/ in the app).
        INSTALL_RPATH "@loader_path"
        BUILD_WITH_INSTALL_RPATH ON)
    if(NOT APPLE)
        set_target_properties(${target} PROPERTIES INSTALL_RPATH "$ORIGIN")
    endif()

    # Ship it with the launcher: in the bundle's Frameworks/ (covered by the
    # app's signature) on iOS, next to the executable elsewhere.
    add_dependencies(${host_target} ${target})
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(_dest "$<TARGET_BUNDLE_CONTENT_DIR:${host_target}>/Frameworks")
    else()
        set(_dest "$<TARGET_FILE_DIR:${host_target}>")
    endif()
    add_custom_command(TARGET ${host_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_dest}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${_dest}"
        COMMENT "Embedding core ${core_id}"
        VERBATIM)

    # The launcher's entry for this core.
    _mc360_cpp_string("${core_id}" _id)
    _mc360_cpp_string("${CORE_NAME}" _name)
    _mc360_cpp_string("${CORE_UPDATE_DIR}" _update)
    _mc360_cpp_string("${CORE_SAVE_DIR}" _save)
    _mc360_cpp_string("${CORE_CACHE_DIR}" _cache)
    if(identity_known)
        set(_known "true")
    else()
        set(_known "false")
    endif()
    if(has_update)
        set(_has_update "true")
    else()
        set(_has_update "false")
    endif()
    set(_entry "    {${_id}, ${_name}, \"$<TARGET_FILE_NAME:${target}>\", ${_known}, 0x${BASE_TITLE_ID}u, 0x${BASE_MEDIA_ID}u, 0x${game_version}u, ${_has_update}, ${_update}, ${_save}, ${_cache}},\n")
    set_property(GLOBAL APPEND_STRING PROPERTY MC360_CORE_TABLE "${_entry}")
    set_property(GLOBAL APPEND PROPERTY MC360_BUILT_CORES "${core_id}")
endfunction()

# -----------------------------------------------------------------------------
# mc360_add_cores(<host_target>): every core in MC360_CORES, plus the
# launcher's generated table of them.
# -----------------------------------------------------------------------------
function(mc360_add_cores host_target)
    if(NOT MC360_CORES)
        file(GLOB _core_files RELATIVE "${MC360_CORES_DIR}" "${MC360_CORES_DIR}/*/core.cmake")
        list(SORT _core_files COMPARE NATURAL)
        set(MC360_CORES "")
        foreach(_f IN LISTS _core_files)
            get_filename_component(_id "${_f}" DIRECTORY)
            list(APPEND MC360_CORES "${_id}")
        endforeach()
    endif()

    set_property(GLOBAL PROPERTY MC360_CORE_TABLE "")
    set_property(GLOBAL PROPERTY MC360_BUILT_CORES "")
    foreach(_id IN LISTS MC360_CORES)
        mc360_add_core(${_id} ${host_target})
    endforeach()

    get_property(_built GLOBAL PROPERTY MC360_BUILT_CORES)
    if(NOT _built)
        message(FATAL_ERROR
            "No title update core could be built. Copy the files from your own disc into "
            "app/game/ and the Title Update 9 default.xexp next to them (see README.md), "
            "or set up another core as described in cores/README.md.")
    endif()
    list(JOIN _built ", " _built_text)
    message(STATUS "Title update cores in this build: ${_built_text}")

    get_property(_table GLOBAL PROPERTY MC360_CORE_TABLE)
    set(_out "${CMAKE_CURRENT_BINARY_DIR}/mc360_cores_table.cpp")
    file(GENERATE OUTPUT "${_out}" CONTENT
"// Generated by cmake/mc360_cores.cmake - do not edit.

#include \"launcher/launcher.h\"

namespace mc360::launcher {

namespace {
// id, name, library, identity known, Title ID, Media ID, version, has update,
// update folder, save folder, cache folder
constexpr CoreInfo kCores[] = {
${_table}};
}  // namespace

std::span<const CoreInfo> BundledCores() {
  return kCores;
}

}  // namespace mc360::launcher
")
    target_sources(${host_target} PRIVATE "${_out}")
endfunction()
