// SPDX-License-Identifier: GPL-3.0-or-later
// Existing BlueWake/Dolphin HLE behind the same build-selected engine contract.
#include "gcnllr/native_dsp.h"
#include "dsp_adapter_c.h"
#include "host_memory.hpp"
#include <algorithm>
#include <limits>
#include <new>
#include <cstdio>
#include <stdexcept>
struct GcnDspEngine {
    GcnDspEngineConfig config{};
    BluewakeDspAdapter* dsp{};
    uint16_t mail_high{};
    bool mail_high_written{},failed{};
};
static GcnDspEngine* owner;
// Build-derived donor bridge retains the existing GameCube creator and adds
// explicit console selection without exposing donor C++ objects across ABI.
extern "C" BluewakeDspAdapter* bluewake_dsp_adapter_create_hle_console(
    void*,BluewakeDspGuestPointerFn,uint8_t*,uint32_t,BluewakeDspTimebaseFn,
    BluewakeDspInterruptFn,int wii);
static bool ready(const GcnDspEngine* e) {return e && e==owner && e->dsp && !e->failed;}
template<class F> static bool checked(GcnDspEngine* e,F&& fn) {
    if(!ready(e))return false;
    try {fn();return !e->failed;}
    catch(const std::exception& error) {
        e->failed=true;
        std::fprintf(stderr,"[native-dsp-hle] %s\n",error.what());
        return false;
    }
}
static uint8_t* pointer(void* user,uint32_t address,uint32_t size) {
    auto& e=*static_cast<GcnDspEngine*>(user);
    try{return gcn_dsp_hle_pointer(e.config,address,size);}
    catch(...){e.failed=true;throw;}
}
static uint64_t timebase(void* user) {
    const auto& c=static_cast<GcnDspEngine*>(user)->config;return c.timebase(c.user);
}
static void interrupt(void* user) {
    const auto& c=static_cast<GcnDspEngine*>(user)->config;c.interrupt(c.user);
}
extern "C" GcnDspEngine* gcn_dsp_engine_create(const GcnDspEngineConfig* c) {
    if(!c || owner || !c->mem1 || !c->mem1_size || !c->aram || c->aram_size!=0x1000000u ||
       (c->wii && (!c->mem2 || !c->mem2_size)) ||
       !c->prepare || !c->written || !c->timebase || !c->interrupt)return nullptr;
    auto* e=new(std::nothrow) GcnDspEngine;if(!e)return nullptr;e->config=*c;
    try {
        e->dsp=bluewake_dsp_adapter_create_hle_console(e,pointer,c->aram,c->aram_size,timebase,interrupt,c->wii?1:0);
        if(!e->dsp || !bluewake_dsp_adapter_is_hle(e->dsp) || e->failed)
            throw std::runtime_error("DSP HLE initialization failed");
        owner=e;return e;
    }catch(const std::exception& error) {
        std::fprintf(stderr,"[native-dsp-hle] initialization: %s\n",error.what());
        bluewake_dsp_adapter_destroy(e->dsp);delete e;return nullptr;
    }
}
extern "C" void gcn_dsp_engine_destroy(GcnDspEngine* e) {
    if(!e || e!=owner)return;
    bluewake_dsp_adapter_destroy(e->dsp);owner=nullptr;delete e;
}
extern "C" bool gcn_dsp_engine_run(GcnDspEngine* e,uint32_t cycles) {
    return checked(e,[&]{
        while(cycles) {
            const auto slice=std::min<uint32_t>(cycles,std::numeric_limits<int>::max());
            bluewake_dsp_adapter_run_cycles(e->dsp,int(slice));
            if(e->failed)throw std::runtime_error("DSP HLE update failed");
            cycles-=slice;
        }
    });
}
extern "C" bool gcn_dsp_engine_read_control(GcnDspEngine* e,uint16_t* out) {
    return out && checked(e,[&]{*out=bluewake_dsp_adapter_read_control(e->dsp);});
}
extern "C" bool gcn_dsp_engine_write_control(GcnDspEngine* e,uint16_t value) {
    return checked(e,[&]{bluewake_dsp_adapter_write_control(e->dsp,value);});
}
extern "C" bool gcn_dsp_engine_read_mail(GcnDspEngine* e,bool cpu,bool low,uint16_t* out) {
    return out && checked(e,[&]{
        const auto value=cpu ? bluewake_dsp_adapter_peek_cpu_mailbox(e->dsp):bluewake_dsp_adapter_peek_dsp_mailbox(e->dsp);
        if(low)*out=cpu ? uint16_t(value):bluewake_dsp_adapter_read_dsp_mailbox_low(e->dsp);
        else *out=cpu && e->mail_high_written ? uint16_t((e->mail_high&0x7fffu)|(value>>16&0x8000u)):uint16_t(value>>16);
    });
}
extern "C" bool gcn_dsp_engine_write_mail(GcnDspEngine* e,bool low,uint16_t value) {
    return checked(e,[&]{
        // The donor consumes protocol payloads, not a hardware mailbox image.
        // Preserve the CPU's raw high half: setting a synthetic busy bit turns
        // Zelda's zero sync into a different command. ROM's status-bearing
        // echo is adapted separately at that service boundary.
        if(low)bluewake_dsp_adapter_write_cpu_mailbox(e->dsp,uint32_t(e->mail_high)<<16|value);
        else {e->mail_high=value;e->mail_high_written=true;}
    });
}
extern "C" uint16_t gcn_dsp_engine_peek_mail_high(const GcnDspEngine* e,bool cpu) {
    if(!e || e!=owner || !e->dsp)return 0;
    const auto value=cpu ? bluewake_dsp_adapter_peek_cpu_mailbox(e->dsp):bluewake_dsp_adapter_peek_dsp_mailbox(e->dsp);
    return cpu && e->mail_high_written ? uint16_t((e->mail_high&0x7fffu)|(value>>16&0x8000u)):uint16_t(value>>16);
}
extern "C" GcnDspEngineStats gcn_dsp_engine_stats(const GcnDspEngine* e) {
    GcnDspEngineStats s{};s.provider=1;s.failed=!ready(e);
    // HLE replaces DSP execution; native-instruction retirement is not a
    // meaningful HLE counter. Do not relabel native host calls as guest DSP.
    if(e && e==owner && e->dsp)s.control=bluewake_dsp_adapter_read_control(e->dsp);
    return s;
}
