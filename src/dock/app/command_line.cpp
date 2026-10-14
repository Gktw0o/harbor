#include "app/command_line.h"

#include <charconv>

#include "core/ipc/pipe_name.h"

namespace harbor::dock
{
namespace
{

constexpr std::wstring_view kPipeName = L"--pipe-name";
constexpr std::wstring_view kDataDir = L"--data-dir";
constexpr std::wstring_view kTestTaskbar = L"--test-taskbar";
constexpr std::wstring_view kTestPingTimeout = L"--test-ping-timeout-ms";

// Arguments echoed into error messages; anything outside printable ASCII becomes '?'.
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

bool IsValidTestId(std::wstring_view id) noexcept
{
    if (id.empty() || id.size() > 64)
    {
        return false;
    }
    for (const wchar_t c : id)
    {
        if (!((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'-'))
        {
            return false;
        }
    }
    return true;
}

} // namespace

std::variant<DockOptions, std::string> ParseCommandLine(std::span<const std::wstring_view> args)
{
    DockOptions options;
    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::wstring_view name = args[i];
        if (name != kPipeName && name != kDataDir && name != kTestTaskbar && name != kTestPingTimeout)
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
        else if (name == kDataDir)
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
        else if (name == kTestTaskbar)
        {
            if (options.testTaskbarId)
            {
                return "--test-taskbar is given more than once";
            }
            if (!IsValidTestId(value))
            {
                return "--test-taskbar must be 1-64 characters from [a-z0-9-]";
            }
            options.testTaskbarId = std::wstring(value);
        }
        else
        {
            if (options.testPingTimeoutMs)
            {
                return "--test-ping-timeout-ms is given more than once";
            }
            const auto ascii = ToAscii(value).value_or("");
            std::uint32_t milliseconds = 0;
            const auto [end, error] =
                std::from_chars(ascii.data(), ascii.data() + ascii.size(), milliseconds);
            if (ascii.empty() || error != std::errc{} || end != ascii.data() + ascii.size() ||
                milliseconds < 100 || milliseconds > 60'000)
            {
                return "--test-ping-timeout-ms must be a number from 100 to 60000";
            }
            options.testPingTimeoutMs = milliseconds;
        }
    }

    // All or nothing: an override can never point a dock at the real taskbar, and a test dock can never
    // join the real Shell or share the real restore record.
    const int overrides =
        options.pipeName.has_value() + options.dataDir.has_value() + options.testTaskbarId.has_value();
    if (overrides != 0 && overrides != 3)
    {
        return "--pipe-name, --data-dir and --test-taskbar must be given together";
    }
    if (options.testPingTimeoutMs && overrides == 0)
    {
        return "--test-ping-timeout-ms needs --pipe-name, --data-dir and --test-taskbar";
    }
    return options;
}

} // namespace harbor::dock
