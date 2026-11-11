// SPDX-License-Identifier: MIT
#include "host_memory.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>
struct Observation{uint32_t address,size;bool write;};
struct Host{std::vector<Observation> prepared,written;bool allow=true;};
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main() {
    try {
        std::array<uint8_t,64> mem1{},mem2{};Host host;GcnDspEngineConfig c{};
        c.mem1=mem1.data();c.mem1_size=mem1.size();c.mem2=mem2.data();c.mem2_size=mem2.size();c.wii=true;c.user=&host;
        c.prepare=[](void* p,uint32_t a,uint32_t n,bool w){auto& h=*static_cast<Host*>(p);h.prepared.push_back({a,n,w});return h.allow;};
        c.written=[](void* p,uint32_t a,uint32_t n){auto& h=*static_cast<Host*>(p);h.written.push_back({a,n,true});return h.allow;};
        auto* one=gcn_dsp_hle_pointer(c,0x80000010,4);auto* two=gcn_dsp_hle_pointer(c,0x90000010,4);
        one[0]=17;two[0]=34;require(mem1[16]==17 && mem2[16]==34,"DSP MEM2 aliases MEM1");
        require(gcn_dsp_hle_pointer(c,0xd0000010,4)==two,"DSP MEM2 alias identity");
        require(host.prepared[1].address==0x10000010 && host.written[1].address==0x10000010,"DSP callback lost physical bank");
        require(host.prepared[1].write && host.written[1].size==4,"DSP pointer notifications changed");
        auto reject=[&](uint32_t a,uint32_t n){bool failed=false;try{(void)gcn_dsp_hle_pointer(c,a,n);}catch(const std::runtime_error&){failed=true;}require(failed,"DSP unmapped span accepted");};
        reject(0x1000003f,2);reject(0x10000041,0);reject(0x08000000,4);
        host.allow=false;reject(0x10000010,4);host.allow=true;
        c.wii=false;reject(0x10000010,4);require(gcn_dsp_hle_pointer(c,0x80000010,4)==one,"GameCube MEM1 changed");
        std::puts("DSP HLE MEM1/MEM2 isolation, aliases, callbacks and rejection passed");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
