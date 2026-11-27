// SPDX-License-Identifier: GPL-3.0-or-later
#include "Core/DSP/DSPCaptureLogger.h"
#include "Common/PcapFile.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace
{
void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}
template <typename T> T field(const std::vector<u8>& bytes, size_t offset)
{
  require(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset, "Truncated PCAP");
  T value;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}
}
int main()
{
  try
  {
    // The writer owns this stream; inspect its bytes before the writer closes it.
    std::FILE* file = std::tmpfile();
    require(file != nullptr, "Cannot create synthetic capture");
    std::vector<u8> bytes;
    {
      // Read back while the writer lives, after an explicit stdio flush.
      auto pcap = std::make_unique<Common::PCAP>(new File::IOFile(file));
      DSP::PCAPDSPCaptureLogger logger(std::move(pcap));
      DSP::DSPCaptureLogger& capture = logger;
      capture.LogIFXRead(0x1234, 0x5678);
      capture.LogIFXWrite(0x2345, 0x6789);
      const std::array<u8, 3> dma = {0x91, 0x42, 0xe7};
      capture.LogDMA(0x123, 0x23456789, 0x4567, dma.size(), dma.data());
      std::vector<u8> large(65535);
      for (size_t i = 0; i < large.size(); ++i)
        large[i] = static_cast<u8>(i * 37);
      capture.LogDMA(2, 0x3456789a, 0x5678, large.size(), large.data());
      capture.LogDMA(3, 0, 0, 0, nullptr);
      bool rejected = false;
      try { capture.LogDMA(0, 0, 0, 1, nullptr); }
      catch (const std::invalid_argument&) { rejected = true; }
      require(rejected, "Invalid capture DMA was accepted");
      require(std::fflush(file) == 0 && std::fseek(file, 0, SEEK_END) == 0, "Capture flush failed");
      const long length = std::ftell(file);
      require(length > 0 && std::fseek(file, 0, SEEK_SET) == 0, "Capture seek failed");
      bytes.resize(static_cast<size_t>(length));
      require(std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "Capture read failed");
    }
    require(field<u32>(bytes, 0) == 0xa1b2c3d4 && field<u16>(bytes, 4) == 2 &&
            field<u16>(bytes, 6) == 4 && field<u32>(bytes, 16) == 65535 &&
            field<u32>(bytes, 20) == 147, "PCAP file header changed");
    size_t at = 24;
    auto packet = [&](u32 captured, u32 actual) {
      require(field<u32>(bytes, at + 4) < 1000000, "PCAP timestamp is invalid");
      require(field<u32>(bytes, at + 8) == captured && field<u32>(bytes, at + 12) == actual,
              "PCAP packet lengths changed");
      const size_t payload = at + 16;
      at = payload + captured;
      return payload;
    };
    auto p = packet(6, 6);
    require(bytes[p] == 0 && bytes[p + 1] == 1 && field<u16>(bytes, p + 2) == 0x1234 &&
            field<u16>(bytes, p + 4) == 0x5678, "IFX read capture changed");
    p = packet(6, 6);
    require(bytes[p] == 0 && bytes[p + 1] == 0 && field<u16>(bytes, p + 2) == 0x2345 &&
            field<u16>(bytes, p + 4) == 0x6789, "IFX write capture changed");
    p = packet(14, 14);
    require(bytes[p] == 1 && field<u16>(bytes, p + 1) == 0x123 &&
            field<u32>(bytes, p + 3) == 0x23456789 && field<u16>(bytes, p + 7) == 0x4567 &&
            field<u16>(bytes, p + 9) == 3 && bytes[p + 11] == 0x91 &&
            bytes[p + 12] == 0x42 && bytes[p + 13] == 0xe7, "DMA capture changed");
    p = packet(65535, 65546);
    require(field<u16>(bytes, p + 9) == 65535, "Maximum DMA length changed");
    for (size_t i = 0; i < 65535 - 11; ++i)
      require(bytes[p + 11 + i] == static_cast<u8>(i * 37), "Maximum DMA bytes changed");
    p = packet(11, 11);
    require(bytes[p] == 1 && field<u16>(bytes, p + 9) == 0 && at == bytes.size(),
            "Zero DMA or invalid-request atomicity changed");
    bool rejected = false;
    try { DSP::PCAPDSPCaptureLogger missing(static_cast<Common::PCAP*>(nullptr)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Null capture writer was accepted");
    std::puts("DSP PCAP IFX read/write, DMA, snapshot bounds and errors passed");
    return 0;
  }
  catch (const std::exception& error)
  {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
