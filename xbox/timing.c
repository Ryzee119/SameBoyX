#include <windows.h>

void GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
    GetSystemTimePreciseAsFileTime(lpSystemTimeAsFileTime);
}

HANDLE CreateWaitableTimer(LPSECURITY_ATTRIBUTES lpTimerAttributes, BOOL bManualReset, LPCWSTR lpTimerName)
{
    //Hack
    static HANDLE hTimer;
    return hTimer;
}

BOOL SetWaitableTimer(HANDLE hTimer, const LARGE_INTEGER *lpDueTime, LONG lPeriod, void *a, void *b, void *c)
{
    //Hack. This isnt meant to wait, instead its meant to setup an object for WaitForSingleObject function to wait.
    LARGE_INTEGER duration;
    duration.QuadPart = lpDueTime->QuadPart;
    while (TRUE)
    {
        NTSTATUS status = KeDelayExecutionThread(UserMode, FALSE, &duration);
        if (status != STATUS_ALERTED)
        {
            return status == STATUS_USER_APC ? WAIT_IO_COMPLETION : 0;
        }
    }
    return TRUE;
}