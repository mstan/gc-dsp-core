// SPDX-License-Identifier: GPL-3.0-or-later
// Small owning stdio adapter for the extracted DSP capture writer.
#pragma once
#include <cstdio>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

namespace File
{
class IOFile final
{
public:
  explicit IOFile(std::FILE* file) : m_file(file)
  {
    if (!m_file)
      throw std::runtime_error("DSP capture file is not open");
  }
  IOFile(const std::string& filename, const char* mode)
  {
#ifdef _WIN32
    const auto wide = [](const std::string& text) {
      const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1,
                                            nullptr, 0);
      if (!length)
        throw std::runtime_error("DSP capture path is not UTF-8");
      std::wstring result(static_cast<size_t>(length), L'\0');
      if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1,
                               result.data(), length))
        throw std::runtime_error("DSP capture path conversion failed");
      return result;
    };
    m_file = _wfopen(wide(filename).c_str(), wide(mode).c_str());
#else
    m_file = std::fopen(filename.c_str(), mode);
#endif
    if (!m_file)
      throw std::runtime_error("Cannot open DSP capture file");
  }
  ~IOFile() { std::fclose(m_file); }
  IOFile(const IOFile&) = delete;
  IOFile& operator=(const IOFile&) = delete;
  bool WriteBytes(const void* bytes, size_t length)
  {
    if (length && std::fwrite(bytes, 1, length, m_file) != length)
      throw std::runtime_error("Cannot write DSP capture file");
    return true;
  }
private:
  std::FILE* m_file = nullptr;
};
}  // namespace File
