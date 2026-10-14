#include "adapters/process_job.h"

#include <windows.h>

namespace harbor::adapters
{

ProcessJob CurrentProcessJob() noexcept
{
    BOOL inJob = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &inJob))
    {
        return ProcessJob::Unknown;
    }
    if (!inJob)
    {
        return ProcessJob::None;
    }
    // With no handle, this queries the job the calling process belongs to.
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    if (!QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits, sizeof(limits),
                                   nullptr))
    {
        return ProcessJob::Unknown;
    }
    return (limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) != 0
               ? ProcessJob::KillOnClose
               : ProcessJob::Ordinary;
}

const char* ToString(ProcessJob job) noexcept
{
    switch (job)
    {
    case ProcessJob::None:
        return "none";
    case ProcessJob::Ordinary:
        return "a job without kill-on-close";
    case ProcessJob::KillOnClose:
        return "a kill-on-close job";
    case ProcessJob::Unknown:
        return "unknown";
    }
    return "unknown";
}

} // namespace harbor::adapters
