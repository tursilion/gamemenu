/*
 * launcher.cpp - Win32 process launch implementation
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "launcher.h"

/*---------------------------------------------------------------------------
 * LaunchAndWait - Launch a child process and block until it exits.
 *
 * CreateProcess requires a mutable copy of the command line string
 * (it may modify the buffer internally), so we copy to a char array.
 *
 * While waiting, we run a minimal message pump so that Windows does not
 * consider this application hung. WaitForSingleObject with a timeout
 * allows the pump to tick periodically.
 *---------------------------------------------------------------------------*/
bool LaunchAndWait(const std::string& commandLine, std::string& error)
{
    /* CreateProcess needs a mutable LPSTR, not a const char*.
     * Copy the command line into a local buffer. */
    char cmdBuf[4096];
    if (commandLine.size() >= sizeof(cmdBuf) - 1)
    {
        error = "Command line too long (max 4095 chars)";
        return false;
    }
    strncpy_s(cmdBuf, sizeof(cmdBuf), commandLine.c_str(), _TRUNCATE);

    STARTUPINFOA        si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);

    /* Launch the process.
     * - lpApplicationName = NULL: parse executable from command line string
     * - bInheritHandles   = FALSE: child does not inherit our handles
     * - dwCreationFlags   = 0: normal priority, same console
     * - lpCurrentDirectory= NULL: inherit our working directory */
    BOOL ok = CreateProcessA(
        NULL,       /* Parse exe from command line */
        cmdBuf,     /* Mutable command line buffer */
        NULL,       /* Default process security */
        NULL,       /* Default thread security */
        FALSE,      /* Do not inherit handles */
        0,          /* No special creation flags */
        NULL,       /* Inherit environment */
        NULL,       /* Inherit current directory */
        &si,
        &pi
    );

    if (!ok)
    {
        DWORD err = GetLastError();
        char msg[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, err, 0, msg, sizeof(msg), NULL);
        error = std::string("CreateProcess failed: ") + msg;
        return false;
    }

    /* We do not need the thread handle */
    CloseHandle(pi.hThread);

    /* Pump messages while waiting so Windows doesn't mark us as hung.
     * Check for child exit every 100ms. */
    MSG msg;
    for (;;)
    {
        DWORD waitResult = WaitForSingleObject(pi.hProcess, 100 /*ms*/);

        if (waitResult == WAIT_OBJECT_0)
        {
            /* Child has exited */
            break;
        }

        /* Drain the Windows message queue */
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    CloseHandle(pi.hProcess);
    return true;
}
