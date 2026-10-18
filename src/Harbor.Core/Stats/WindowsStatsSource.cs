using Harbor.Core.Interop;

namespace Harbor.Core.Stats;

/// <summary>
/// The stats from Windows (docs/adr/0016): GetSystemTimes for the total CPU, PDH counters added by their English
/// names (any UI language) for each processor and the GPU engines, GlobalMemoryStatusEx, the fixed drives and the
/// battery. Each part that isn't available is left out. Not thread-safe: <see cref="StatsSampler"/> calls it.
/// </summary>
public sealed unsafe class WindowsStatsSource : IStatsSource
{
    private readonly nint _query;
    private readonly nint _cores;
    private readonly nint _gpu;
    private CpuTimes? _lastTimes;
    private bool _disposed;

    public WindowsStatsSource()
    {
        if (SystemNative.PdhOpenQuery(0, 0, out _query) != 0)
        {
            _query = 0;
            return;
        }
        // Either may be missing (no GPU counters on some drivers): that part is then left out.
        if (SystemNative.PdhAddEnglishCounter(_query, @"\Processor Information(*)\% Processor Utility", 0, out _cores) != 0)
        {
            _cores = 0;
        }
        if (SystemNative.PdhAddEnglishCounter(_query, @"\GPU Engine(*)\Utilization Percentage", 0, out _gpu) != 0)
        {
            _gpu = 0;
        }
        // Rates need two collections: the first is now, so the first Read already has numbers.
        _ = SystemNative.PdhCollectQueryData(_query);
    }

    public StatsReading Read()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        double? cpu = null;
        if (SystemNative.GetSystemTimes(out var idle, out var kernel, out var user))
        {
            var now = new CpuTimes(idle.Ticks, kernel.Ticks, user.Ticks);
            cpu = _lastTimes is { } last ? StatsMath.CpuUse(last, now) : null;
            _lastTimes = now;
        }

        IReadOnlyList<double> cores = [];
        double? gpu = null;
        if (_query != 0 && SystemNative.PdhCollectQueryData(_query) == 0)
        {
            cores = _cores != 0 ? StatsMath.CoreUse(Values(_cores)) : [];
            gpu = _gpu != 0 ? StatsMath.GpuUse(Values(_gpu)) : null;
        }

        var memory = new SystemNative.MemoryStatusEx { Length = (uint)sizeof(SystemNative.MemoryStatusEx) };
        var hasMemory = SystemNative.GlobalMemoryStatusEx(ref memory);

        return new StatsReading(
            cpu,
            cores,
            hasMemory ? memory.TotalPhys - memory.AvailPhys : null,
            hasMemory ? memory.TotalPhys : null,
            gpu,
            Drives(),
            Battery());
    }

    // One counter's instances and their values; empty if PDH has none (yet).
    private static List<(string Instance, double Value)> Values(nint counter)
    {
        var values = new List<(string, double)>();
        uint size = 0;
        if (SystemNative.PdhGetFormattedCounterArray(counter, SystemNative.PdhFmtDouble, ref size, out _, null) != SystemNative.PdhMoreData || size == 0)
        {
            return values;
        }
        var buffer = new byte[size];
        fixed (byte* items = buffer)
        {
            if (SystemNative.PdhGetFormattedCounterArray(counter, SystemNative.PdhFmtDouble, ref size, out var count, items) != 0)
            {
                return values;
            }
            var item = (SystemNative.PdhFmtCounterValueItem*)items;
            for (var i = 0; i < count; i++)
            {
                if (SystemNative.IsValid(item[i].CStatus) && item[i].Name != null)
                {
                    values.Add((new string(item[i].Name), item[i].Value));
                }
            }
        }
        return values;
    }

    private static List<DriveSpace> Drives()
    {
        var drives = new List<DriveSpace>();
        foreach (var drive in DriveInfo.GetDrives())
        {
            try
            {
                if (drive.DriveType == DriveType.Fixed && drive.IsReady)
                {
                    drives.Add(new DriveSpace(drive.Name.TrimEnd('\\'), (ulong)drive.AvailableFreeSpace, (ulong)drive.TotalSize));
                }
            }
            catch (IOException)
            {
                // A drive that went away while being read.
            }
            catch (UnauthorizedAccessException)
            {
            }
        }
        return drives;
    }

    private static BatteryState? Battery()
    {
        // BatteryFlag 128: no battery; 255: unknown status. BatteryLifePercent 255: unknown.
        if (!SystemNative.GetSystemPowerStatus(out var status) || status.BatteryFlag is 128 or 255 || status.BatteryLifePercent > 100)
        {
            return null;
        }
        var charging = (status.BatteryFlag & 8) != 0;
        TimeSpan? left = status.BatteryLifeTime == uint.MaxValue || status.AcLineStatus == 1 ? null : TimeSpan.FromSeconds(status.BatteryLifeTime);
        return new BatteryState(status.BatteryLifePercent, charging, left);
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }
        _disposed = true;
        if (_query != 0)
        {
            _ = SystemNative.PdhCloseQuery(_query);
        }
    }
}
