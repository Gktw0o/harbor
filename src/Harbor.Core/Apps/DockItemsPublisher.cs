using System.Threading.Channels;
using Harbor.Core.Ipc;
using Harbor.Core.Logging;
using Harbor.Core.Settings;

namespace Harbor.Core.Apps;

/// <summary>
/// Keeps the dock's list in step with the settings. Looking apps up takes shell calls, which can be slow, so it
/// happens on this loop; when settings change faster than that, only the newest version is built. Icons no list
/// uses any more are deleted from <paramref name="icons"/>.
/// </summary>
public sealed class DockItemsPublisher(IPinResolver resolver, ILog log, IconStore? icons = null)
{
    private readonly Channel<HarborSettings> _pending = Channel.CreateBounded<HarborSettings>(
        new BoundedChannelOptions(1) { FullMode = BoundedChannelFullMode.DropOldest, SingleReader = true });

    /// <summary>Raised on the loop with each new list.</summary>
    public event Action<DockItems>? Built;

    public void Update(HarborSettings settings) => _pending.Writer.TryWrite(settings);

    public async Task RunAsync(CancellationToken cancellationToken)
    {
        await foreach (var settings in _pending.Reader.ReadAllAsync(cancellationToken).ConfigureAwait(false))
        {
            var items = await Task.Run(() => DockItemsBuilder.Build(settings, resolver, log), cancellationToken).ConfigureAwait(false);
            log.Info($"dock items: {items.Items.Count} of {settings.Pinned.Count} pinned apps are on the dock");
            Built?.Invoke(items);
            icons?.KeepOnly(items.Items.Select(item => item.Icon).OfType<string>());
        }
    }
}
