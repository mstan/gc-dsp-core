// Copyright 2014 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from Dolphin 6de526c684ee6dd8b6fc3447dae858ae480ccbb5:
// DSPCaptureLogger.cpp and Common/PcapFile.cpp. See UPSTREAM.md.
#include "Core/DSP/DSPCaptureLogger.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>
#include "Common/PcapFile.h"

namespace Common
{
namespace
{
constexpr u32 CAPTURE_LENGTH = 65535;
#pragma pack(push, 1)
struct PCAPHeader
{
  u32 magic;
  u16 major, minor;
  s32 timezone;
  u32 accuracy, capture_length, link_type;
};
struct PCAPRecordHeader { u32 seconds, microseconds, captured, actual; };
#pragma pack(pop)
static_assert(sizeof(PCAPHeader) == 24 && sizeof(PCAPRecordHeader) == 16);
}
PCAP::PCAP(File::IOFile* file, LinkType type) : m_file(file)
{
  if (!m_file)
    throw std::invalid_argument("DSP capture needs a file");
  const PCAPHeader header = {0xa1b2c3d4, 2, 4, 0, 0, CAPTURE_LENGTH, static_cast<u32>(type)};
  m_file->WriteBytes(&header, sizeof(header));
}
void PCAP::AddPacket(const u8* bytes, size_t size)
{
  if ((!bytes && size) || size > std::numeric_limits<u32>::max())
    throw std::invalid_argument("Invalid DSP capture packet");
  const auto timestamp = std::chrono::system_clock::now().time_since_epoch();
  const u32 captured = static_cast<u32>(std::min<size_t>(size, CAPTURE_LENGTH));
  const PCAPRecordHeader header = {
      static_cast<u32>(std::chrono::duration_cast<std::chrono::seconds>(timestamp).count()),
      static_cast<u32>(std::chrono::duration_cast<std::chrono::microseconds>(timestamp).count() %
                       1000000), captured, static_cast<u32>(size)};
  m_file->WriteBytes(&header, sizeof(header));
  m_file->WriteBytes(bytes, captured);
}
}  // namespace Common

namespace DSP
{
#pragma pack(push, 1)
struct IFXAccessPacket { u8 magic, is_read; u16 address, value; };
struct DMAPacket { u8 magic; u16 control; u32 gc_address; u16 dsp_address, length; };
#pragma pack(pop)
static_assert(sizeof(IFXAccessPacket) == 6 && sizeof(DMAPacket) == 11);
PCAPDSPCaptureLogger::PCAPDSPCaptureLogger(const std::string& filename)
    : m_pcap(std::make_unique<Common::PCAP>(new File::IOFile(filename, "wb"))) {}
PCAPDSPCaptureLogger::PCAPDSPCaptureLogger(Common::PCAP* pcap) : m_pcap(pcap)
{
  if (!m_pcap)
    throw std::invalid_argument("DSP capture needs a PCAP writer");
}
PCAPDSPCaptureLogger::PCAPDSPCaptureLogger(std::unique_ptr<Common::PCAP>&& pcap)
    : PCAPDSPCaptureLogger(pcap.release()) {}
PCAPDSPCaptureLogger::~PCAPDSPCaptureLogger() = default;
void PCAPDSPCaptureLogger::LogIFXAccess(bool read, u16 address, u16 value)
{
  const IFXAccessPacket packet = {0, static_cast<u8>(read), address, value};
  m_pcap->AddPacket(packet);
}
void PCAPDSPCaptureLogger::LogDMA(u16 control, u32 gc_address, u16 dsp_address, u16 length,
                                  const u8* data)
{
  if (!data && length)
    throw std::invalid_argument("DSP capture needs DMA bytes");
  // A maximum DMA plus its header exceeds the old fixed 64 KiB buffer.
  std::vector<u8> buffer(sizeof(DMAPacket) + length);
  const DMAPacket packet = {1, control, gc_address, dsp_address, length};
  std::memcpy(buffer.data(), &packet, sizeof(packet));
  if (length)
    std::memcpy(buffer.data() + sizeof(packet), data, length);
  m_pcap->AddPacket(buffer.data(), buffer.size());
}
}  // namespace DSP
