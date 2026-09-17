cmake_minimum_required(VERSION 3.10)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Define VITASDK")
  endif()
endif()
project(ssx_vita C)
include("${VITASDK}/share/vita.cmake" REQUIRED)
set(VITA_APP_NAME "SSX")
set(VITA_TITLEID  "SSXVITA01")
set(VITA_VERSION  "00.01")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q,--allow-multiple-definition -D_GNU_SOURCE -Wall -O3 -mfloat-abi=softfp")
add_executable(ssx_vita
  loader/main.c loader/so_util.c loader/jni_patch.c loader/stubs.c loader/default_dynlib.c loader/pthread_bridge.c)
target_link_libraries(ssx_vita
  vitaGL vitashark SceShaccCgExt taihen_stub kubridge_stub mathneon m stdc++ pthread
  SceAppMgr_stub SceAudio_stub SceCtrl_stub SceTouch_stub SceCommonDialog_stub SceDisplay_stub
  SceGxm_stub SceKernelDmacMgr_stub SceLibKernel_stub SceNet_stub SceNetCtl_stub SceShaccCg_stub
  SceSysmodule_stub ScePower_stub SceIofilemgr_stub)
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")  # extended memory mode (+~109MB)
vita_create_self(eboot.bin ssx_vita UNSAFE)
vita_create_vpk(SSX.vpk ${VITA_TITLEID} eboot.bin VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME})
