# Static project dependencies with the standard MSVC DLL runtime (/MD, /MDd).
# Leave VCPKG_BUILD_TYPE unset so both Debug and Release are available.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET v143)
