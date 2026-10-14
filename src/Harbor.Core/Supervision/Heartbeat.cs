namespace Harbor.Core.Supervision;

/// <summary>
/// Counts the dock's unanswered pings in a row. It counts pings, not time, so a sleep and resume (when
/// neither process runs) can't look like a hang. Not thread-safe: the watchdog's loop owns it.
/// </summary>
public sealed class Heartbeat(int missedLimit)
{
    private uint? _latest;
    private bool _answered;

    /// <summary>Pings in a row that got no answer before the next one went out.</summary>
    public int Missed { get; private set; }

    public bool IsHung => Missed >= missedLimit;

    /// <summary>Call when ping <paramref name="seq"/> goes out: an unanswered previous ping counts as missed.</summary>
    public void Sent(uint seq)
    {
        if (_latest is not null && !_answered)
        {
            Missed++;
        }
        _latest = seq;
        _answered = false;
    }

    /// <summary>A pong arrived; only one for the latest ping counts.</summary>
    public void Answered(uint seq)
    {
        if (seq == _latest)
        {
            _answered = true;
            Missed = 0;
        }
    }

    /// <summary>A new connection (or none): nothing is outstanding.</summary>
    public void Reset()
    {
        _latest = null;
        _answered = false;
        Missed = 0;
    }
}
