/*
 * config.cpp - Configuration file parser implementation
 */

#include "config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

/*---------------------------------------------------------------------------
 * trim - Remove leading and trailing whitespace from a string in-place.
 *---------------------------------------------------------------------------*/
static void trim(std::string& s)
{
    /* Trim leading whitespace */
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
        [](unsigned char c) { return !std::isspace(c); }));

    /* Trim trailing whitespace */
    s.erase(std::find_if(s.rbegin(), s.rend(),
        [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
}

/*---------------------------------------------------------------------------
 * toLower - Return a lowercase copy of the input string.
 *---------------------------------------------------------------------------*/
static std::string toLower(const std::string& s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return out;
}

/*---------------------------------------------------------------------------
 * LoadConfig - Parse the config file and populate the entries vector.
 *
 * Parsing strategy:
 *   1. Read line by line.
 *   2. Skip blanks and comment lines (# or ;).
 *   3. An [Entry] section header starts a new pending entry.
 *   4. key = value lines populate the pending entry's fields.
 *   5. When a new [Entry] is encountered (or EOF), commit the pending
 *      entry if all three fields are present.
 *---------------------------------------------------------------------------*/
bool LoadConfig(const std::string& path,
                std::vector<MenuEntry>& entries,
                std::string& error, int& timeoutVal)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        error = "Cannot open config file: " + path;
        return false;
    }

    entries.clear();

    MenuEntry   pending;
    bool        inEntry     = false;  /* Are we inside an [Entry] block? */
    bool        hasName     = false;
    bool        hasLaunch   = false;
    bool        hasShot     = false;
    int         lineNum     = 0;

    auto commitPending = [&]()
    {
        /* Only commit if all three required fields were found */
        if (inEntry && hasName && hasLaunch && hasShot)
        {
            if ((int)entries.size() < MAX_ENTRIES)
            {
                entries.push_back(pending);
            }
        }
        /* Reset pending state */
        pending   = MenuEntry{};
        inEntry   = false;
        hasName   = false;
        hasLaunch = false;
        hasShot   = false;
    };

    std::string line;
    while (std::getline(file, line))
    {
        ++lineNum;
        trim(line);

        /* Skip blank lines */
        if (line.empty()) continue;

        /* Skip comment lines */
        if (line[0] == '#' || line[0] == ';') continue;

        /* Section header: [Entry] (we accept any bracketed section name
         * as a new entry block, for flexibility) */
        if (line.front() == '[' && line.back() == ']')
        {
            commitPending();
            inEntry = true;
            continue;
        }

        /* Split on first '=' */
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;  /* Malformed line, skip */

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        trim(key);
        trim(val);

        std::string keyLower = toLower(key);

        /* Must be inside a section to process key=value pairs */
        /* except for AttractTimeout */
        if (!inEntry) {
            if (keyLower == "attracttimeout") {
                int n = atoi(val.c_str());
                if (n != 0) {
                    timeoutVal = n;
                }
            }
            continue;
        }

        if (keyLower == "name")
        {
            pending.name = val;
            hasName = true;
        }
        else if (keyLower == "launch")
        {
            pending.launchString = val;
            hasLaunch = true;
        }
        else if (keyLower == "screenshot")
        {
            pending.screenshotPath = val;
            hasShot = true;
        }
        else if (keyLower == "folder")
        {
            pending.folderPath = val;
            hasShot = true;
        }
        /* Unknown keys are silently ignored */
    }

    /* Commit the final pending entry after EOF */
    commitPending();

    return true;
}
