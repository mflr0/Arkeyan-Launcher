# Toolchain for the Windows build in tools/ci/windows.Dockerfile: clang-cl, lld-link and the CRT and SDK splatted by xwin.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER lld-link)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT llvm-mt)
set(CMAKE_AR llvm-lib)

set(XWIN_ROOT /opt/xwin CACHE PATH "xwin splat output")
set(_includes
    ${XWIN_ROOT}/crt/include ${XWIN_ROOT}/sdk/include/ucrt ${XWIN_ROOT}/sdk/include/um ${XWIN_ROOT}/sdk/include/shared
    ${XWIN_ROOT}/sdk/include/winrt)
set(_flags "--target=x86_64-pc-windows-msvc")
set(_rc_flags "")
foreach(dir ${_includes})
    string(APPEND _flags " /imsvc${dir}")
    string(APPEND _rc_flags " -I${dir}")
endforeach()
set(CMAKE_C_FLAGS_INIT "${_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_flags}")
set(CMAKE_RC_FLAGS_INIT "${_rc_flags}")

set(_libs "/libpath:${XWIN_ROOT}/crt/lib/x86_64 /libpath:${XWIN_ROOT}/sdk/lib/um/x86_64 /libpath:${XWIN_ROOT}/sdk/lib/ucrt/x86_64")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_libs}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_libs}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_libs}")

# xwin splats only the release CRT: the compiler checks link without the debug one.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
