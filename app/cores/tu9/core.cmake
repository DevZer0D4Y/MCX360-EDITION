# Title Update 9 core. Read by cmake/mc360_cores.cmake.
#
# The game version, Title ID and Media ID this core expects are not written
# here: they are read from the exact default.xex/default.xexp codegen used, so
# they always match the recompiled code.

set(CORE_NAME "Title Update 9")

# Folders inside the app's Documents folder. TU9 was the only build before the
# launcher existed, so it keeps the folders that build used: existing installs
# carry on with their title update, worlds, settings and shader cache as they
# are. New cores leave these unset and get tu<N>/, userdata-tu<N>/ and
# cache-tu<N>/.
set(CORE_UPDATE_DIR "tu")
set(CORE_SAVE_DIR "userdata")
set(CORE_CACHE_DIR "cache")
