// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gcnllr/native_dsp.h"
#include <stdexcept>

inline uint8_t* gcn_dsp_hle_pointer(const GcnDspEngineConfig& c,uint32_t address,uint32_t size,bool writing=true) {
    const uint32_t physical=address&0x1fffffffu;
    uint8_t* bank=c.mem1;uint32_t offset=physical,available=c.mem1_size;
    if(c.wii && physical>=0x10000000u) {
        bank=c.mem2;offset=physical-0x10000000u;available=c.mem2_size;
    }
    // Mutable borrowed spans conservatively announce writes. Explicit sample
    // and word reads only observe memory, preserving its physical bank identity.
    if(!bank || offset>available || size>available-offset ||
       !c.prepare(c.user,physical,size,writing) || (writing && !c.written(c.user,physical,size)))
        throw std::runtime_error("DSP host memory observation/write rejected");
    return bank+offset;
}
