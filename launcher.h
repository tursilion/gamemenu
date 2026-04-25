#pragma once
/*
 * launcher.h - Process launch and wait for emulator frontend
 *
 * Uses Win32 CreateProcess to launch the emulator, then blocks
 * (with a message pump to keep Windows happy) until the child exits.
 * Returns control to the caller once the child process has terminated.
 */

#include <string>

/*---------------------------------------------------------------------------
 * LaunchAndWait - Parse 'commandLine' into executable + args, launch via
 * CreateProcess, and block until the child process exits.
 *
 * 'commandLine' is the full command line string as read from config,
 * e.g. "C:\emu\mame.exe -rompath roms pacman"
 *
 * Returns true if the process launched successfully (regardless of child
 * exit code), false if CreateProcess failed.
 * 'error' is set on failure.
 *---------------------------------------------------------------------------*/
bool LaunchAndWait(const std::string& commandLine, std::string& error);
