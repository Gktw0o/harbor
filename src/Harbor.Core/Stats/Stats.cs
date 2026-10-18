using System.Globalization;

namespace Harbor.Core.Stats;

/// <summary>Processor times since boot, in 100 ns ticks (GetSystemTimes; kernel includes idle).</summary>
public readonly record struct CpuTimes(ulong Idle, ulong Kernel, ulong User);

/// <summary>One drive's space, in bytes.</summary>
public sealed record DriveSpace(string Name, ulong Free, ulong Total)
{
    public double UsedShare => Total == 0 ? 0 : 1 - ((double)Free / Total);
}

/// <summary>The battery, if there is one.</summary>
/// <param name="Percent">0-100.</param>
/// <param name="Charging">On power and charging.</param>
/// <param name="TimeLeft">Until empty, when Windows knows.</param>
public sealed record BatteryState(int Percent, bool Charging, TimeSpan? TimeLeft);

/// <summary>
/// What the island's Stats section shows (docs/adr/0016). Every part is optional: a source that isn't
/// available on this machine (GPU counters on a driver without them, no battery) reports nothing, never fails.
/// </summary>
/// <param name="Cpu">Total use, 0-100.</param>
/// <param name="Cores">Each logical processor's use, 0-100.</param>
/// <param name="Gpu">The busiest engine type's use, 0-100 (as Task Manager shows it).</param>
public sealed record StatsReading(
    double? Cpu,
    IReadOnlyList<double> Cores,
    ulong? MemoryUsed,
    ulong? MemoryTotal,
    double? Gpu,
    IReadOnlyList<DriveSpace> Drives,
    BatteryState? Battery)
{
    public static StatsReading Empty { get; } = new(null, [], null, null, null, [], null);
}

/// <summary>The arithmetic behind the readings, pure.</summary>
public static class StatsMath
{
    /// <summary>
    /// Total processor use between two GetSystemTimes readings, 0-100; null if no time passed or the counters went
    /// backwards (a wrap, or readings out of order).
    /// </summary>
    public static double? CpuUse(CpuTimes before, CpuTimes after)
    {
        if (after.Idle < before.Idle || after.Kernel < before.Kernel || after.User < before.User)
        {
            return null;
        }
        var idle = after.Idle - before.Idle;
        var total = (after.Kernel - before.Kernel) + (after.User - before.User);
        if (total == 0)
        {
            return null;
        }
        return Math.Clamp(100.0 * (total - Math.Min(idle, total)) / total, 0, 100);
    }

    /// <summary>
    /// GPU use from the "GPU Engine(*)\Utilization Percentage" instances (names like
    /// <c>pid_1234_luid_..._phys_0_eng_0_engtype_3D</c>): each engine type's instances summed, the busiest type
    /// taken, as Task Manager does. Null without instances.
    /// </summary>
    public static double? GpuUse(IEnumerable<(string Instance, double Value)> engines)
    {
        ArgumentNullException.ThrowIfNull(engines);
        var byType = new Dictionary<string, double>(StringComparer.OrdinalIgnoreCase);
        foreach (var (instance, value) in engines)
        {
            var at = instance.LastIndexOf("engtype_", StringComparison.OrdinalIgnoreCase);
            var type = at >= 0 ? instance[(at + "engtype_".Length)..] : "";
            byType[type] = byType.GetValueOrDefault(type) + Math.Max(0, value);
        }
        return byType.Count == 0 ? null : Math.Clamp(byType.Values.Max(), 0, 100);
    }

    /// <summary>
    /// The logical processors' use from "Processor Information(*)\% Processor Utility" instances ("0,0", "0,1", ...
    /// plus totals such as "_Total" and "0,_Total", left out), in processor order, each 0-100.
    /// </summary>
    public static IReadOnlyList<double> CoreUse(IEnumerable<(string Instance, double Value)> processors)
    {
        ArgumentNullException.ThrowIfNull(processors);
        return [.. processors
            .Select(p => (Key: Key(p.Instance), p.Value))
            .Where(p => p.Key is not null)
            .OrderBy(p => p.Key!.Value.Group).ThenBy(p => p.Key!.Value.Number)
            .Select(p => Math.Clamp(p.Value, 0, 100))];

        static (int Group, int Number)? Key(string instance)
        {
            var parts = instance.Split(',');
            return parts.Length == 2 &&
                   int.TryParse(parts[0], NumberStyles.None, CultureInfo.InvariantCulture, out var group) &&
                   int.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out var number)
                ? (group, number)
                : null;
        }
    }

    /// <summary>Bytes for people: "512 MB", "12.3 GB", "1.5 TB" (binary units, one decimal from GB up).</summary>
    public static string Size(ulong bytes, IFormatProvider? culture = null)
    {
        culture ??= CultureInfo.CurrentCulture;
        const double Gb = 1024.0 * 1024 * 1024;
        return bytes switch
        {
            >= 1024UL * 1024 * 1024 * 1024 => string.Format(culture, "{0:0.0} TB", bytes / (Gb * 1024)),
            >= 1024UL * 1024 * 1024 => string.Format(culture, "{0:0.0} GB", bytes / Gb),
            _ => string.Format(culture, "{0:0} MB", bytes / (1024.0 * 1024)),
        };
    }

    /// <summary>A battery's time left for people: "2 h 05 min", "45 min".</summary>
    public static string Duration(TimeSpan time) =>
        time.TotalHours >= 1
            ? string.Format(CultureInfo.InvariantCulture, "{0} h {1:00} min", (int)time.TotalHours, time.Minutes)
            : string.Format(CultureInfo.InvariantCulture, "{0} min", Math.Max(0, (int)time.TotalMinutes));
}
