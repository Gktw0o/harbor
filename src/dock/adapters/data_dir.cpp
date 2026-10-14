#include "adapters/data_dir.h"

#include <windows.h>

#include <knownfolders.h>
#include <shlobj_core.h>
#include <wil/resource.h>

namespace harbor::adapters
{

std::filesystem::path DefaultDataDir() noexcept
try
{
    wil::unique_cotaskmem_string localAppData;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData)))
    {
        return {};
    }
    return std::filesystem::path(localAppData.get()) / L"Harbor";
}
catch (...)
{
    return {};
}

} // namespace harbor::adapters
