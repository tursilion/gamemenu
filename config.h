#pragma once
/*
 * config.h - Configuration file parser for emulator frontend
 *
 * Config file format (simple INI-like, no JSON/XML):
 *
 *   [Entry]
 *   name = My Game Title
 *   launch = C:\Emulators\mame.exe -rompath roms pacman
 *   screenshot = C:\Screenshots\pacman.png
 *
 * Rules:
 *   - Sections begin with [Entry] (case-insensitive)
 *   - Keys are name, launch, screenshot
 *   - Lines beginning with # or ; are comments
 *   - Blank lines are ignored
 *   - Up to MAX_ENTRIES entries supported
 */

#include <string>
#include <vector>

#define MAX_ENTRIES 20

/* Single menu entry loaded from config */
struct MenuEntry
{
    std::string name;           /* Display name shown in menu */
    std::string launchString;   /* Full command line to execute */
    std::string screenshotPath; /* Path to PNG screenshot file */
    std::string folderPath;     /* ignored unless set, then we cd */
};

/* Loads all entries from the config file at 'path'.
 * Returns true on success, false if file could not be opened.
 * Partial entries (missing fields) are silently skipped.
 * 'error' is populated with a human-readable message on failure. */
bool LoadConfig(const std::string& path,
                std::vector<MenuEntry>& entries,
                std::string& error, int& timeoutVal);
