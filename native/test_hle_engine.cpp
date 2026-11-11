// SPDX-License-Identifier: MIT
#include "gcnllr/native_dsp.h"
#include "Core/DSP/DSPHost.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
struct Host{uint32_t prepared{},written{},address{};};
void exercise(bool wii) {
    std::vector<uint8_t> mem1(64),mem2(64),aram(0x1000000);Host host;GcnDspEngineConfig c{};
    c.mem1=mem1.data();c.mem1_size=mem1.size();c.mem2=mem2.data();c.mem2_size=mem2.size();
    c.aram=aram.data();c.aram_size=aram.size();c.wii=wii;c.user=&host;
    c.prepare=[](void* p,uint32_t a,uint32_t,bool){auto& h=*static_cast<Host*>(p);++h.prepared;h.address=a;return true;};
    c.written=[](void* p,uint32_t a,uint32_t){auto& h=*static_cast<Host*>(p);++h.written;h.address=a;return true;};
    c.timebase=[](void*)->uint64_t{return 0;};c.interrupt=[](void*){};
    auto* engine=gcn_dsp_engine_create(&c);need(engine!=nullptr,"Native DSP HLE create failed");
    try {
        need(DSP::Host::IsWiiHost()==wii,"DSP donor received wrong console");
        need(gcn_dsp_engine_create(&c)==nullptr,"Concurrent DSP HLE owner accepted");
        // Release halt while retaining DSPInit; clearing DSPInit enters the
        // separate audio initialization microcode rather than the ROM service.
        need(gcn_dsp_engine_write_control(engine,0x0800),"DSP ROM halt release failed");
        auto mail=[&](uint32_t value){return gcn_dsp_engine_write_mail(engine,false,uint16_t(value>>16)) && gcn_dsp_engine_write_mail(engine,true,uint16_t(value));};
        uint16_t hi=0,lo=0;
        auto read=[&]{need(gcn_dsp_engine_read_mail(engine,false,false,&hi) && gcn_dsp_engine_read_mail(engine,false,true,&lo),"DSP ROM mailbox read failed");return uint32_t(hi)<<16|lo;};
        need(read()==0x8071feed,"DSP ROM initial mail changed");
        need(mail(0x1234abcd),"DSP ROM ping rejected");need(read()==0xfeee9234,"DSP ROM high-half/status echo changed");
        need(gcn_dsp_engine_run(engine,1024),"DSP ROM update failed");
        const auto stats=gcn_dsp_engine_stats(engine);need(stats.provider==1 && !stats.failed && !stats.interpreter_instructions && !stats.misses,"DSP HLE silently executed interpreter");
        auto& bank=wii?mem2:mem1;bank[8]=0x12;bank[9]=0x34;bank[10]=0x56;bank[11]=0x78;
        auto parameter=[&](uint32_t command,uint32_t value){need(mail(command) && mail(value),"DSP ROM task parameter rejected");};
        parameter(0x80f3a001,wii?0x90000008:0x80000008);parameter(0x80f3a002,4);parameter(0x80f3b002,0);parameter(0x80f3c002,0);
        need(mail(0x80f3d001),"DSP ROM start selector rejected");
        need(!mail(0),"Unknown DSP microcode silently selected a generic HLE");
        need(host.prepared==1 && host.written==1 && host.address==(wii?0x10000008u:8u),"DSP ROM upload lost selected memory bank");
        need(gcn_dsp_engine_stats(engine).failed,"Unsupported DSP task did not fail closed");
    }catch(...){gcn_dsp_engine_destroy(engine);throw;}
    gcn_dsp_engine_destroy(engine);
}
int main(){try{exercise(false);exercise(true);std::puts("GC/Wii DSP HLE ROM, memory-bank task and strict-unknown checks passed");return 0;}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
