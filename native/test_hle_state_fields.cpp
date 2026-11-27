// SPDX-License-Identifier: MIT
// Exercise the actual adapted shared HLE serializer with poisoned ABI padding.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "Common/ChunkFile.h"
#include "Core/HW/DSPHLE/DSPHLE.h"
#include "Core/HW/DSPHLE/UCodes/UCodes.h"
#include "Core/System.h"
#include <cassert>
#include <algorithm>
#include <cstring>
#include <vector>

class UploadState final : public DSP::HLE::UCodeInterface {
public:
  explicit UploadState(DSP::HLE::DSPHLE& hle) : UCodeInterface(&hle, 0) {}
  void Initialize() override {}
  void HandleMail(u32) override {}
  void Update() override {}
  void DoState(PointerWrap& p) override { DoStateShared(p); }
  static std::vector<u8> input() {
    return {1,0x44,0x33,0x22,0x11,0x66,0x55,0x88,0x77,0xcc,0xbb,0xaa,0x99,
        0x34,0x12,0x78,0x56,0xbc,0x9a,0xdf,0x9b,0x57,0x13,0x68,0x24,0xe0,0xac,
        9,0,0,0,1};
  }
  void set(unsigned char poison) {
    auto data=input();decode(data);
    // Locate the distinct uploaded field values in this actual object's byte
    // representation. Touch only the two ABI padding bytes; use the real
    // serializer to set/observe every private semantic member.
    auto* begin=reinterpret_cast<u8*>(this);auto* end=begin+sizeof(*this);
    auto* fields=std::search(begin,end,data.begin()+1,data.begin()+19);
    assert(fields!=end && fields+28<=end);
    assert(std::equal(fields+20,fields+28,data.begin()+19));
    fields[18]=fields[19]=poison;
    assert(encode()==data);
  }
  void change(unsigned field) {
    constexpr unsigned offsets[]={1,5,7,9,13,15,17,19,23,25,0,27,31};
    assert(field<13);auto data=input();data[offsets[field]]^=1;decode(data);
  }
  std::vector<u8> encode() {
    std::vector<u8> data(128);u8* cursor=data.data();
    PointerWrap p(&cursor,data.size(),PointerWrap::Mode::Write);DoState(p);
    assert(p.IsWriteMode());data.resize(size_t(cursor-data.data()));return data;
  }
  void decode(std::vector<u8>& data) {
    u8* cursor=data.data();PointerWrap p(&cursor,data.size(),PointerWrap::Mode::Read);
    DoState(p);assert(p.IsReadMode() && cursor==data.data()+data.size());
  }
};
int main() {
  DSP::HLE::DSPHLE hle(Core::System::GetInstance());
  UploadState a(hle),b(hle);a.set(0xaa);b.set(0x55);
  auto expected=a.encode();assert(expected.size()==32 && expected==b.encode());
  for(unsigned i=0;i<13;++i) {
    b.set(0xcc);b.change(i);assert(b.encode()!=expected);
    b.decode(expected);assert(b.encode()==expected);
  }
  return 0;
}
