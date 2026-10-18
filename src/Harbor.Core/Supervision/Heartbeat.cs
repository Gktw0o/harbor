namespace Harbor.Core.Supervision;

/// <summary>
/// Counts the dock's unanswered pings in a row. It counts pings, not time, so a sleep and resume (when
/// neither process runs) can't look like a hang. Not thread-safe: the watchdog's loop owns it.
/// </summary>
/// <param name="missedLimit">Misses in a row that make a running dock hung.</param>
/// <param name="firstAnswerLimit">
/// Misses allowed before a connection's first answer: a dock that just connected may still be setting up on its
/// UI thread, which answers the pings. Never fewer than <paramref name="missedLimit"/>.
/// </param>
public sealed class Heartbeat(int missedLimit, int? firstAnswerLimit = null)
{
    private readonly int _firstAnswerLimit = Math.Max(missedLimit, firstAnswerLimit ?? missedLimit);
    private uint? _latest;
    private bool _answered;
    private bool _everAnswered;

    /// <summary>Pings in a row that got no answer before the next one went out.</summary>
    public int Missed { get; private set; }

    public bool IsHung => Missed >= (_everAnswered ? missedLimit : _firstAnswerLimit);

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
            _everAnswered = true;
            Missed = 0;
        }
    }

    /// <summary>A new connection (or none): nothing is outstanding, and the dock may be starting again.</summary>
    public void Reset()
    {
        _latest = null;
        _answered = false;
        _everAnswered = false;
        Missed = 0;
    }
}
