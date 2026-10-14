#include "app/command_line.h"

#include "core/ipc/pipe_name.h"

namespace harbor::dock
{
namespace
{

constexpr std::wstring_view kPipeName = L"--pipe-name";
constexpr std::wstring_view kDataDir = L"--data-dir";

// Arguments echoed into error messages; anything outside ASCII becomes '?'.
std::string Printable(std::wstring_view text)
{
    std::string result;
    for (const wchar_t c : text)
    {
        result.push_back(c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?');
    }
    return result;
}

std::optional<std::string> ToAscii(std::wstring_view text)
{
    std::string result;
    for (const wchar_t c : text)
    {
        if (c > 0x7F)
        {
            return std::nullopt;
        }
        result.push_back(static_cast<char>(c));
    }
    return result;
}

} // namespace

std::variant<DockOptions, std::string> ParseCommandLine(std::span<const std::wstring_view> args)
{
    DockOptions options;
    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::wstring_view name = args[i];
        if (name != kPipeName && name != kDataDir)
        {
            return "unknown argument: " + Printable(name);
        }
        if (i + 1 == args.size())
        {
            return Printable(name) + " needs a value";
        }
        const std::wstring_view value = args[++i];

        if (name == kPipeName)
        {
            if (options.pipeName)
            {
                return "--pipe-name is given more than once";
            }
            auto pipeName = ToAscii(value);
            if (!pipeName || !ipc::IsValidPipeName(*pipeName))
            {
                return "--pipe-name must be \"harbor.\" followed by [a-z0-9.-]";
            }
            options.pipeName = std::move(*pipeName);
        }
        else
        {
            if (options.dataDir)
            {
                return "--data-dir is given more than once";
            }
            const std::filesystem::path dataDir(value);
            if (!dataDir.is_absolute())
            {
                return "--data-dir must be an absolute path";
            }
            options.dataDir = dataDir.lexically_normal();
        }
    }
    return options;
}

} // namespace harbor::dock
