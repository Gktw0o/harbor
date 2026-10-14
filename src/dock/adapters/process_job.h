#pragma once

namespace harbor::adapters
{

enum class ProcessJob
{
    None,
    // In a job that doesn't kill its processes when its last handle closes.
    Ordinary,
    // In a job with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE: whoever holds the job can end the dock with no chance
    // to restore the taskbar, together with Harbor Shell (e.g. a terminal or IDE closing).
    KillOnClose,
    Unknown,
};

// The job this process runs in, as far as it can tell: only the innermost job of a nested chain is checked.
ProcessJob CurrentProcessJob() noexcept;

const char* ToString(ProcessJob job) noexcept;

} // namespace harbor::adapters
