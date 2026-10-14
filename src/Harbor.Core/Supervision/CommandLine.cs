using System.Buffers;
using System.Text;

namespace Harbor.Core.Supervision;

/// <summary>Builds a Windows command line that CommandLineToArgvW (which the dock uses) splits back exactly.</summary>
public static class CommandLine
{
    private static readonly SearchValues<char> NeedsQuotes = SearchValues.Create(" \t\n\v\"");

    public static string Join(IEnumerable<string> arguments) => string.Join(' ', arguments.Select(Quote));

    public static string Quote(string argument)
    {
        ArgumentNullException.ThrowIfNull(argument);
        if (argument.Length > 0 && !argument.AsSpan().ContainsAny(NeedsQuotes))
        {
            return argument;
        }
        // Backslashes are literal unless they precede a quote; then each one must be doubled.
        var quoted = new StringBuilder("\"");
        var backslashes = 0;
        foreach (var c in argument)
        {
            if (c == '\\')
            {
                backslashes++;
                continue;
            }
            quoted.Append('\\', c == '"' ? (backslashes * 2) + 1 : backslashes).Append(c);
            backslashes = 0;
        }
        return quoted.Append('\\', backslashes * 2).Append('"').ToString();
    }
}
