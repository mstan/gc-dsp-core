// Copyright 2014 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <memory>
#include "Common/CommonTypes.h"
#include "Common/IOFile.h"

namespace Common
{
class PCAP final
{
public:
  enum class LinkType : u32 { Ethernet = 1, User = 147 };
  explicit PCAP(File::IOFile* file, LinkType link_type = LinkType::User);
  template <typename T> void AddPacket(const T& object)
  {
    AddPacket(reinterpret_cast<const u8*>(&object), sizeof(object));
  }
  void AddPacket(const u8* bytes, size_t size);
private:
  std::unique_ptr<File::IOFile> m_file;
};
}  // namespace Common
