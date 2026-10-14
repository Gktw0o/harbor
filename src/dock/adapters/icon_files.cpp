#include "adapters/icon_files.h"

#include <windows.h>

#include <wil/resource.h>

#include "core/island/icon_file.h"

namespace harbor::adapters
{

std::vector<std::uint8_t> ReadIconFile(const std::filesystem::path& iconsDir, std::string_view name)
{
    const auto path = iconsDir / std::filesystem::path(std::u8string(name.begin(), name.end()));
    // Shared for delete too: the Shell may clean the folder up while the dock reads.
    const wil::unique_hfile file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    LARGE_INTEGER size{};
    if (!file || !GetFileSizeEx(file.get(), &size) || size.QuadPart <= 0 ||
        static_cast<unsigned long long>(size.QuadPart) > island::kMaxIconFileBytes)
    {
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    if (!ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) ||
        read != bytes.size())
    {
        return {};
    }
    return bytes;
}

} // namespace harbor::adapters
