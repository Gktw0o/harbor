using System.Diagnostics;

namespace Harbor.Core.Tests;

// tests/dock/test_windows.cpp: off-screen app windows of the class "Harbor.Test.<id>.App", the only windows a
// test dock with that id follows (docs/adr/0010). scripts/build.ps1 points HARBOR_TEST_WINDOWS_EXE at it.
internal sealed class TestWindows : IDisposable
{
    private readonly Process _process;

    public TestWindows(string id)
    {
        var exe = Environment.GetEnvironmentVariable("HARBOR_TEST_WINDOWS_EXE");
        if (string.IsNullOrEmpty(exe))
        {
            if (string.Equals(Environment.GetEnvironmentVariable("CI"), "true", StringComparison.OrdinalIgnoreCase))
            {
                Assert.Fail("HARBOR_TEST_WINDOWS_EXE must be set in CI; scripts/build.ps1 sets it.");
            }
            Assert.Inconclusive("Set HARBOR_TEST_WINDOWS_EXE to a built harbor_test_windows.exe; scripts/build.ps1 does.");
        }
        var startInfo = new ProcessStartInfo(exe)
        {
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            CreateNoWindow = true,
        };
        startInfo.ArgumentList.Add(id);
        _process = Process.Start(startInfo)!;
    }

    // Sends one command (see test_windows.cpp) and returns its answer; anything but "ok..." fails the test.
    public async Task<string> SendAsync(string command)
    {
        await _process.StandardInput.WriteLineAsync(command);
        await _process.StandardInput.FlushAsync();
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        var answer = await _process.StandardOutput.ReadLineAsync(timeout.Token);
        Assert.IsNotNull(answer, $"{command}: no answer");
        Assert.IsTrue(answer.StartsWith("ok", StringComparison.Ordinal), $"{command}: {answer}");
        return answer;
    }

    public void Dispose()
    {
        try
        {
            // End of input quits it.
            _process.StandardInput.Close();
            if (!_process.WaitForExit(5000))
            {
                _process.Kill();
            }
        }
        catch (InvalidOperationException)
        {
        }
        _process.Dispose();
    }
}
