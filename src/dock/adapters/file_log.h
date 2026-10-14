#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string_view>
#include <wil/resource.h>

#include "ports/logger.h"

namespace harbor::adapters
{

// ILogger that appends UTF-8 lines to <directory>\<name>.log and mirrors them to the debugger. A line that
// would take the file past maxBytes first moves it to <name>.old.log (replacing the previous one), so the
// logs never use more than about twice maxBytes. With an empty directory it logs to the debugger only.
class FileLog final : public log::ILogger
{
  public:
    static constexpr std::uint64_t kDefaultMaxBytes = 1024 * 1024;

    FileLog(const std::filesystem::path& directory, std::wstring_view name,
            std::uint64_t maxBytes = kDefaultMaxBytes);
    FileLog(const FileLog&) = delete;
    FileLog& operator=(const FileLog&) = delete;

    void Write(log::Level level, std::string_view message) noexcept override;

  private:
    void OpenLocked() noexcept;
    void RollOverLocked() noexcept;

    std::filesystem::path m_path;
    std::filesystem::path m_oldPath;
    const std::uint64_t m_maxBytes;

    std::mutex m_mutex;
    wil::unique_hfile m_file;
    std::uint64_t m_size = 0;
};

} // namespace harbor::adapters
