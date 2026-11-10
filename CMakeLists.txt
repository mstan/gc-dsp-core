# DSP LLE: vendored Dolphin DSP-LLE interpreter core (see UPSTREAM.md) + net-new
# Host binding (src/host.cpp) and C API (src/dsp_lle_c.cpp). Built as a C++
# static library the runtime links; runtime/src/dsp.c calls the C API.
file(GLOB GCN_DSP_LLE_SRC CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/Core/DSP/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/Core/DSP/Interpreter/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(gcn_dsp_lle STATIC ${GCN_DSP_LLE_SRC})
target_sources(gcn_dsp_lle PRIVATE Core/DSP/Aot/Aot.cpp)
if(GCNLLR_DSP STREQUAL "AOT")
  if(NOT EXISTS "${GCNLLR_DSP_AOT_DIR}/sources.cmake")
    message(FATAL_ERROR "DSP AOT requires a generated static catalog in GCNLLR_DSP_AOT_DIR")
  endif()
  include("${GCNLLR_DSP_AOT_DIR}/sources.cmake")
  target_sources(gcn_dsp_lle PRIVATE ${GCNLLR_DSP_AOT_SOURCES})
  target_compile_definitions(gcn_dsp_lle PRIVATE GCNLLR_DSP_AOT=1 GCNLLR_DSP_AOT_CATALOG=1)
  if(GCNLLR_STATIC_CPU)
    target_compile_definitions(gcn_dsp_lle PRIVATE GCNLLR_DSP_AOT_STRICT=1)
  endif()
endif()
set_target_properties(gcn_dsp_lle PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# src/ is public so linkers get the C API header (dsp_lle_c.h); the vendored
# tree root is private (resolves the "Core/DSP/*" + shimmed "Common/*" includes).
target_include_directories(gcn_dsp_lle PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(gcn_dsp_lle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The vendored TUs assume Dolphin's precompiled header (CommonTypes/Logging/
# Assert always visible); force-include the shimmed equivalents to match.
target_compile_options(gcn_dsp_lle PRIVATE
  "SHELL:-include Common/CommonTypes.h"
  "SHELL:-include Common/Logging/Log.h"
  "SHELL:-include Common/Assert.h")
