#include "adapters/file_log.h"

#include <string>

namespace harbor::adapters
{
namespace
{

void WriteToDebugger(std::string_view utf8)
{
    // OutputDebugStringA would read the UTF-8 bytes in the ANSI code page.
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (length <= 0)
    {
        return;
    }
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
    OutputDebugStringW(wide.c_str());
}

} // namespace

FileLog::FileLog(const std::filesystem::path& directory, std::wstring_view name, std::uint64_t maxBytes)
    : m_maxBytes(maxBytes)
{
    if (directory.empty())
    {
        return;
    }
    // A failure here shows up as the file not opening; the debugger still gets every line.
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    m_path = directory / (std::wstring(name) + L".log");
    m_oldPath = directory / (std::wstring(name) + L".old.log");
}

void FileLog::Write(log::Level level, std::string_view message) noexcept
try
{
    SYSTEMTIME now{};
    GetSystemTime(&now);
    const std::string line = log::FormatLine(
        {now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds}, level,
        message);
    WriteToDebugger(line);
    if (m_path.empty())
    {
        return;
    }

    std::lock_guard lock(m_mutex);
    if (m_file && m_size > 0 && m_size + line.size() > m_maxBytes)
    {
        RollOverLocked();
    }
    if (!m_file)
    {
        OpenLocked();
    }
    DWORD written = 0;
    if (m_file && WriteFile(m_file.get(), line.data(), static_cast<DWORD>(line.size()), &written, nullptr))
    {
        m_size += written;
    }
}
catch (...)
{
    // Logging must never take the dock down.
}

void FileLog::OpenLocked() noexcept
{
    // FILE_APPEND_DATA without FILE_WRITE_DATA: every write lands at the end of the file.
    m_file.reset(CreateFileW(m_path.c_str(), FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr));
    LARGE_INTEGER size{};
    m_size = m_file && GetFileSizeEx(m_file.get(), &size) ? static_cast<std::uint64_t>(size.QuadPart) : 0;
}

void FileLog::RollOverLocked() noexcept
{
    m_file.reset();
    // Fails while another process has the file open without FILE_SHARE_DELETE (e.g. an editor). Then keep
    // appending, and try again once another maxBytes has been written.
    const bool moved = MoveFileExW(m_path.c_str(), m_oldPath.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
    OpenLocked();
    if (!moved)
    {
        m_size = 0;
    }
}

} // namespace harbor::adapters
