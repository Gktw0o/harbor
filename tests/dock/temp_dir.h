#pragma once

#include <windows.h>

#include <combaseapi.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace harbor::test
{

// A fresh directory under %TEMP%, removed with everything in it when this goes out of scope.
class TempDir
{
  public:
    explicit TempDir(std::wstring_view suffix = L"")
    {
        GUID id{};
        CoCreateGuid(&id);
        wchar_t text[39]{};
        StringFromGUID2(id, text, 39);
        m_path = std::filesystem::temp_directory_path() /
                 (L"harbor-test-" + std::wstring(text) + std::wstring(suffix));
        std::filesystem::create_directories(m_path);
    }

    ~TempDir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& Path() const noexcept
    {
        return m_path;
    }

  private:
    std::filesystem::path m_path;
};

// The whole file as bytes; empty if it doesn't exist.
inline std::string ReadFileBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

} // namespace harbor::test
