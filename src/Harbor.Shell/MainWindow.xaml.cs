using System.Diagnostics;
using Harbor.Core.Ipc;
using Harbor.Core.Logging;
using Microsoft.UI.Xaml;

namespace Harbor.Shell;

public sealed partial class MainWindow : Window
{
    private readonly string _pipeName;
    private readonly FileLog _log;

    public MainWindow(FileLog log)
    {
        InitializeComponent();
        _log = log;

        using var process = Process.GetCurrentProcess();
        _pipeName = PipeNames.For(PipeNames.ControlChannel, (uint)process.SessionId);
        StatusText.Text = $"Waiting for the dock on {_pipeName}";

        var server = new ControlPipeServer(_pipeName, TimeSpan.FromSeconds(1), log.Warn);
        server.StatusChanged += status => DispatcherQueue.TryEnqueue(() => StatusText.Text = Describe(status));

        // Never disposed on purpose: it uses no timer or wait handle, and the server may still observe the
        // token after the window closes.
        var shutdown = new CancellationTokenSource();
        Closed += (_, _) => shutdown.Cancel();
        _ = RunServerAsync(server, shutdown.Token);
    }

    private async Task RunServerAsync(ControlPipeServer server, CancellationToken cancellationToken)
    {
        try
        {
            await server.RunAsync(cancellationToken);
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            _log.Error($"couldn't serve {_pipeName}: {exception.Message}");
            StatusText.Text = $"Another process already owns {_pipeName}. Is Harbor Shell already running?";
        }
    }

    private string Describe(ControlLinkStatus status) => status switch
    {
        { Connected: false } => $"Waiting for the dock on {_pipeName}",
        { LastPongSeq: { } seq, RoundTrip: { } roundTrip } => $"Dock connected · pong {seq} · {roundTrip.TotalMilliseconds:0.0} ms",
        _ => "Dock connected",
    };
}
