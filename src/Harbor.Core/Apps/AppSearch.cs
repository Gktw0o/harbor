using System.Globalization;
using System.Text;
using Harbor.Core.Ipc;

namespace Harbor.Core.Apps;

/// <summary>
/// An installed app as Start lists it (shell:AppsFolder): its name, and its id there (an AppUserModelID, or a
/// path under a known folder). Test indexes may give an absolute path instead.
/// </summary>
public sealed record AppEntry(string Name, string Id)
{
    /// <summary>How the dock starts it: an absolute path as a path, anything else through shell:AppsFolder.</summary>
    public AppTarget Target => Path.IsPathFullyQualified(Id) ? new AppTarget(DockItemKind.Path, Id) : new AppTarget(DockItemKind.App, Id);
}

/// <summary>One result of a search, from any source (apps now; files and web plugins later: docs/adr/0016).</summary>
public sealed record SearchResult(string Title, AppTarget Target);

/// <summary>A source the island's search box asks (docs/adr/0016). Search must be quick: it runs on every key press.</summary>
public interface ISearchProvider
{
    IReadOnlyList<SearchResult> Search(string query, int max);
}

/// <summary>
/// Finding apps by name as the user types (docs/adr/0016), pure. Better matches first: the whole name starting
/// with the query, then a word of it, then its initials ("vsc": Visual Studio Code), then anywhere in it; shorter
/// names before longer ones. Case, accents and the Turkish dotted and dotless i don't matter. The names are folded
/// once, so a query over a few thousand apps takes well under a millisecond.
/// </summary>
public sealed class AppSearch : ISearchProvider
{
    private readonly Entry[] _entries;

    public AppSearch(IEnumerable<AppEntry> apps)
    {
        ArgumentNullException.ThrowIfNull(apps);
        _entries = [.. apps.Select(app => new Entry(app, Fold(app.Name)))];
    }

    public int Count => _entries.Length;

    /// <summary>Up to max apps matching the query, best first; nothing for an empty query.</summary>
    public IReadOnlyList<AppEntry> Find(string query, int max)
    {
        ArgumentNullException.ThrowIfNull(query);
        var folded = Fold(query.Trim());
        if (folded.Length == 0 || max <= 0)
        {
            return [];
        }
        var matches = new List<(int Rank, Entry Entry)>();
        foreach (var entry in _entries)
        {
            var rank = Rank(entry, folded);
            if (rank > 0)
            {
                matches.Add((rank, entry));
            }
        }
        matches.Sort((a, b) =>
        {
            var byRank = b.Rank.CompareTo(a.Rank);
            if (byRank != 0)
            {
                return byRank;
            }
            var byLength = a.Entry.Folded.Length.CompareTo(b.Entry.Folded.Length);
            return byLength != 0 ? byLength : string.CompareOrdinal(a.Entry.Folded, b.Entry.Folded);
        });
        return [.. matches.Take(max).Select(match => match.Entry.App)];
    }

    public IReadOnlyList<SearchResult> Search(string query, int max) =>
        [.. Find(query, max).Select(app => new SearchResult(app.Name, app.Target))];

    // 4: the name starts with it; 3: a word does; 2: the initials do; 1: it is somewhere; 0: no match.
    private static int Rank(Entry entry, string query)
    {
        var name = entry.Folded;
        if (name.StartsWith(query, StringComparison.Ordinal))
        {
            return 4;
        }
        if (entry.WordStarts.Any(start => string.CompareOrdinal(name, start, query, 0, query.Length) == 0))
        {
            return 3;
        }
        if (entry.Initials.StartsWith(query, StringComparison.Ordinal))
        {
            return 2;
        }
        return name.Contains(query, StringComparison.Ordinal) ? 1 : 0;
    }

    /// <summary>
    /// Text as the search compares it: lower case without accents (ü→u, ç→c, ğ→g, é→e), the Turkish ı and İ as i,
    /// and any run of spaces and punctuation as one space.
    /// </summary>
    public static string Fold(string text)
    {
        ArgumentNullException.ThrowIfNull(text);
        var decomposed = text.Normalize(NormalizationForm.FormD);
        var folded = new StringBuilder(decomposed.Length);
        var space = false;
        foreach (var c in decomposed)
        {
            if (CharUnicodeInfo.GetUnicodeCategory(c) == UnicodeCategory.NonSpacingMark)
            {
                continue;
            }
            if (!char.IsLetterOrDigit(c))
            {
                space = folded.Length > 0;
                continue;
            }
            if (space)
            {
                folded.Append(' ');
                space = false;
            }
            // ı (dotless) and İ (dotted capital, which FormD splits into I and a dot) both read as i.
            folded.Append(c == 'ı' ? 'i' : char.ToLowerInvariant(c));
        }
        return folded.ToString();
    }

    private sealed class Entry
    {
        public Entry(AppEntry app, string folded)
        {
            App = app;
            Folded = folded;
            var starts = new List<int>();
            var initials = new StringBuilder();
            for (var i = 0; i < folded.Length; i++)
            {
                if (folded[i] != ' ' && (i == 0 || folded[i - 1] == ' '))
                {
                    starts.Add(i);
                    initials.Append(folded[i]);
                }
            }
            WordStarts = [.. starts];
            Initials = initials.ToString();
        }

        public AppEntry App { get; }
        public string Folded { get; }
        public int[] WordStarts { get; }
        public string Initials { get; }
    }
}
