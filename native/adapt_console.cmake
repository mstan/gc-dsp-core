# SPDX-License-Identifier: MIT
# Derive the console-aware adapter beside its source headers. The pinned donor
# checkout remains immutable, including the original GameCube creator ABI.
function(_gcn_dsp_replace variable label before after)
  string(FIND "${${variable}}" "${before}" _at)
  if(_at EQUAL -1)
    message(FATAL_ERROR "Pinned DSP console seam changed: ${label}")
  endif()
  string(LENGTH "${before}" _length)
  math(EXPR _next "${_at}+${_length}")
  string(SUBSTRING "${${variable}}" ${_next} -1 _tail)
  string(FIND "${_tail}" "${before}" _again)
  if(NOT _again EQUAL -1)
    message(FATAL_ERROR "Ambiguous DSP console seam: ${label}")
  endif()
  string(REPLACE "${before}" "${after}" _text "${${variable}}")
  set(${variable} "${_text}" PARENT_SCOPE)
endfunction()

function(gcnllr_adapt_dsp_console target)
  get_target_property(_done "${target}" GCNLLR_DSP_CONSOLE_CONTRACT)
  if(_done)
    return()
  endif()
  set(_output "${CMAKE_BINARY_DIR}/gcnllr-dsp/adapter")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${GC_DSP_DONOR_HOST_DIR}/dsp_hle_backend.h")
  file(READ "${GC_DSP_DONOR_HOST_DIR}/dsp_hle_backend.h" _header)
  _gcn_dsp_replace(_header "HLE console declaration" "BluewakeDspInterruptFn interrupt);" "BluewakeDspInterruptFn interrupt, bool wii = false, BluewakeDspGuestPointerFn read_pointer = nullptr, const std::uint8_t* coefficients = nullptr, std::uint32_t coefficient_size = 0);")
  # Retain the qualified GameCube pending-work contract for older Wii host
  # donors without replacing their CPU/runtime sources. New donors keep their
  # existing implementation; the exact21-line owner extension is composed only
  # when its declaration is absent.
  set(_bridge_pending FALSE)
  string(FIND "${_header}" "bool update_pending() const;" _pending_at)
  if(_pending_at EQUAL -1)
    set(_bridge_pending TRUE)
    _gcn_dsp_replace(_header "HLE pending declaration" "  int run_cycles(int cycles);"
      "  int run_cycles(int cycles);\n  bool update_pending() const;")
  endif()
  file(CONFIGURE OUTPUT "${_output}/dsp_hle_backend.h" CONTENT "${_header}" @ONLY)
  get_target_property(_sources "${target}" SOURCES)
  set(_adapted)
  set(_count 0)
  foreach(_source IN LISTS _sources)
    get_filename_component(_name "${_source}" NAME)
    if(_name STREQUAL "dsp_hle_backend.cpp")
      file(READ "${_source}" _text)
      _gcn_dsp_replace(_text "HLE console definition" "BluewakeDspTimebaseFn timebase, BluewakeDspInterruptFn interrupt)" "BluewakeDspTimebaseFn timebase, BluewakeDspInterruptFn interrupt, bool wii, BluewakeDspGuestPointerFn read_pointer, const std::uint8_t* coefficients, std::uint32_t coefficient_size)")
      _gcn_dsp_replace(_text "HLE read callback storage" "BluewakeDspGuestPointerFn guest_pointer = nullptr;" "BluewakeDspGuestPointerFn guest_pointer = nullptr;\n  BluewakeDspGuestPointerFn guest_read = nullptr;")
      _gcn_dsp_replace(_text "HLE read callback initialize" "g_host.guest_pointer = guest_pointer_fn;" "g_host.guest_pointer = guest_pointer_fn;\n  g_host.guest_read = read_pointer != nullptr ? read_pointer : guest_pointer_fn;")
      _gcn_dsp_replace(_text "HLE read pointer helper" "}  // namespace\n\nextern \"C\" u64" "u8* guest_read(u32 address, size_t size)\n{\n  if (g_host.guest_read == nullptr || size > 0xFFFFFFFFu) return nullptr;\n  return g_host.guest_read(g_host.user, address, static_cast<u32>(size));\n}\n}  // namespace\n\nextern \"C\" u64")
      _gcn_dsp_replace(_text "Wii accelerator reads main memory" "  return g_host.aram != nullptr ? g_host.aram[address & (g_host.aram_size - 1)] : 0;" "  if (gcnllr_dsp_hle_wii) { const u8* p = guest_read(address, 1); return p != nullptr ? *p : 0; }\n  return g_host.aram != nullptr ? g_host.aram[address & (g_host.aram_size - 1)] : 0;")
      _gcn_dsp_replace(_text "Wii accelerator writes main memory" "  if (g_host.aram != nullptr)\n    g_host.aram[address & (g_host.aram_size - 1)] = value;" "  if (gcnllr_dsp_hle_wii) { if (u8* p = guest_pointer(address, 1)) *p = value; return; }\n  if (g_host.aram != nullptr)\n    g_host.aram[address & (g_host.aram_size - 1)] = value;")
      _gcn_dsp_replace(_text "HLE read16 observes without writing" "const u8* p = guest_pointer(address, 2);" "const u8* p = guest_read(address, 2);")
      _gcn_dsp_replace(_text "HLE read32 observes without writing" "const u8* p = guest_pointer(address, 4);" "const u8* p = guest_read(address, 4);")
      _gcn_dsp_replace(_text "HLE console clock" "return 486000000u;" "return gcnllr_dsp_hle_wii ? 729000000u : 486000000u;")
      _gcn_dsp_replace(_text "HLE console initialization" "return m_impl->hle.Initialize(false, false);" "gcnllr_dsp_hle_wii = wii;\n  gcnllr_dsp_hle_drom = coefficients;\n  gcnllr_dsp_hle_drom_size = coefficient_size;\n  return m_impl->hle.Initialize(wii, false);")
      _gcn_dsp_replace(_text "HLE host flag lifetime" "g_host = HleHost{};\n}" "g_host = HleHost{};\n  gcnllr_dsp_hle_wii = false;\n  gcnllr_dsp_hle_drom = nullptr;\n  gcnllr_dsp_hle_drom_size = 0;\n}")
      if(_bridge_pending)
        _gcn_dsp_replace(_text "HLE pending storage" "  bool interrupt_pending = false;"
          "  bool interrupt_pending = false;\n  bool update_requested = true;")
        _gcn_dsp_replace(_text "HLE pending query/update" "int HleBackend::run_cycles(int cycles)\n{"
[=[bool HleBackend::update_pending() const
{
  // The pinned HLE update methods either send a resume mail after a guest
  // ucode upload, reassert a queued mail, or do nothing. Preserve the next
  // update slot after a write and any interrupt deferred by a mailbox read.
  return g_host.update_requested || g_host.interrupt_pending ||
         m_impl->hle.AccessMailHandler().HasPending();
}

int HleBackend::run_cycles(int cycles)
{
  g_host.update_requested = false;]=])
        _gcn_dsp_replace(_text "HLE control schedules update" "void HleBackend::write_control(std::uint16_t value)\n{"
          "void HleBackend::write_control(std::uint16_t value)\n{\n  g_host.update_requested = true;")
        _gcn_dsp_replace(_text "HLE mail schedules update" "  m_impl->hle.DSP_WriteMailBoxHigh(true, static_cast<u16>(value >> 16));"
          "  g_host.update_requested = true;\n  m_impl->hle.DSP_WriteMailBoxHigh(true, static_cast<u16>(value >> 16));")
        _gcn_dsp_replace(_text "HLE restore schedules update" "  return read.IsReadMode() && read_ptr == data + size;"
          "  g_host.update_requested = true;\n  return read.IsReadMode() && read_ptr == data + size;")
      endif()
      # Restoring a checkpoint must restore the pending-work latch too. A new
      # update after load changes the next hardware deadline and guest IRQ order.
      _gcn_dsp_replace(_text "HLE pending work checkpoint" "  p.Do(g_host.interrupt_pending);"
        "  p.Do(g_host.update_requested);\n  p.Do(g_host.interrupt_pending);")
      _gcn_dsp_replace(_text "HLE restore keeps pending work" "  g_host.update_requested = true;\n  return read.IsReadMode() && read_ptr == data + size;"
        "  return read.IsReadMode() && read_ptr == data + size;")
      set(_text "#include <cstdint>\nbool gcnllr_dsp_hle_wii = false;\nconst std::uint8_t* gcnllr_dsp_hle_drom = nullptr;\nstd::uint32_t gcnllr_dsp_hle_drom_size = 0;\n${_text}")
    elseif(_name STREQUAL "dsp_adapter.cpp")
      file(READ "${_source}" _text)
      _gcn_dsp_replace(_text "DSP host console query" "bool IsWiiHost()\n{\n  return false;\n}" "bool IsWiiHost()\n{\n  return gcnllr_dsp_hle_wii;\n}")
      set(_text "extern bool gcnllr_dsp_hle_wii;\n${_text}")
    elseif(_name STREQUAL "dsp_adapter_c.cpp")
      file(READ "${_source}" _text)
      string(REGEX MATCH "extern \"C\" BluewakeDspAdapter\\* bluewake_dsp_adapter_create_hle\\([^}]+\n\\}" _creator "${_text}")
      if(NOT _creator)
        message(FATAL_ERROR "Pinned DSP HLE adapter creator changed")
      endif()
      string(REPLACE "bluewake_dsp_adapter_create_hle(" "bluewake_dsp_adapter_create_hle_console(" _creator "${_creator}")
      _gcn_dsp_replace(_creator "C console creator" "BluewakeDspInterruptFn interrupt)" "BluewakeDspInterruptFn interrupt, int wii, BluewakeDspGuestPointerFn read_pointer, const uint8_t* coefficients, uint32_t coefficient_size)")
      _gcn_dsp_replace(_creator "C console forwarding" "interrupt))" "interrupt, wii != 0, read_pointer, coefficients, coefficient_size))")
      string(APPEND _text "\n${_creator}\n")
      if(_bridge_pending)
        string(APPEND _text [=[
extern "C" int bluewake_dsp_adapter_update_pending(const BluewakeDspAdapter* adapter)
{
  return adapter != nullptr && (!adapter->hle || adapter->hle->update_pending());
}
]=])
      endif()
    else()
      list(APPEND _adapted "${_source}")
      continue()
    endif()
    math(EXPR _count "${_count}+1")
    file(CONFIGURE OUTPUT "${_output}/${_name}" CONTENT "${_text}" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_source}")
    list(APPEND _adapted "${_output}/${_name}")
  endforeach()
  if(NOT _count EQUAL 3)
    message(FATAL_ERROR "Expected three pinned DSP host adapter sources")
  endif()
  target_include_directories("${target}" PRIVATE "${GC_DSP_DONOR_HOST_DIR}")
  set_property(TARGET "${target}" PROPERTY SOURCES "${_adapted}")
  set_property(TARGET "${target}" PROPERTY GCNLLR_DSP_CONSOLE_CONTRACT "explicit-gc-wii-v1")
endfunction()
