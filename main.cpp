/*
 * main.cpp - Emulator Frontend
 *
 * Star Trek LCARS-inspired console interface.
 * Uses SDL2 for window/input/texture management, Dear ImGui for UI,
 * and stb_image (single-header) for PNG loading.
 *
 * Controls (joystick):
 *   Axis 1 (Y) or D-pad Up/Down  - Navigate menu entries
 *   Button 0 (A/Cross)           - Launch selected entry
 *   Button 1 (B/Circle)          - (reserved / back - no-op at top level)
 *
 * Keyboard fallback (for testing without joystick):
 *   Up/Down arrows               - Navigate
 *   Enter                        - Launch
 *   Escape                       - Quit
 *
 * Dependencies (all header/source drop-in, no separate install):
 *   SDL2          - https://libsdl.org
 *   Dear ImGui    - https://github.com/ocornut/imgui  (backends: SDL2 + SDL2Renderer)
 *   stb_image.h   - https://github.com/nothings/stb
 */

/* ---- stb_image: single-header PNG loader, implementation in this TU ---- */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

/* ---- SDL2 ---- */
#include <SDL.h>

/* ---- Dear ImGui ---- */
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

/* ---- Project headers ---- */
#include "config.h"
#include "launcher.h"

/* ---- Standard library ---- */
#include <string>
#include <vector>
#include <cstdio>
#include <cmath>    /* sinf, for pulse animation */
#include <cstring>
#include <ctime>

/* =========================================================================
 * Constants / tunables
 * ========================================================================= */

/* Path to the config file, relative to the executable's working directory */
static const char* CONFIG_PATH = "frontend.cfg";

/* LCARS-inspired color palette (Star Trek TNG era) */
static const ImVec4 COL_BG           = { 0.02f, 0.02f, 0.08f, 1.0f }; /* Near-black blue */
static const ImVec4 COL_PANEL        = { 0.04f, 0.04f, 0.14f, 1.0f }; /* Dark panel */
static const ImVec4 COL_ORANGE       = { 1.00f, 0.60f, 0.10f, 1.0f }; /* LCARS orange */
static const ImVec4 COL_PEACH        = { 1.00f, 0.75f, 0.55f, 1.0f }; /* LCARS peach */
static const ImVec4 COL_LAVENDER     = { 0.70f, 0.60f, 1.00f, 1.0f }; /* LCARS lavender */
static const ImVec4 COL_BLUE_LIGHT   = { 0.40f, 0.75f, 1.00f, 1.0f }; /* LCARS light blue */
static const ImVec4 COL_BLUE_MID     = { 0.20f, 0.50f, 0.90f, 1.0f }; /* Mid blue */
static const ImVec4 COL_RED          = { 1.00f, 0.20f, 0.10f, 1.0f }; /* Alert red */
static const ImVec4 COL_TEXT_MAIN    = { 0.95f, 0.90f, 0.80f, 1.0f }; /* Warm white text */
static const ImVec4 COL_TEXT_DIM     = { 0.50f, 0.55f, 0.65f, 1.0f }; /* Dimmed label text */
static const ImVec4 COL_SEL_BG       = { 0.15f, 0.30f, 0.55f, 1.0f }; /* Selected item bg */
static const ImVec4 COL_SEL_BORDER   = { 0.40f, 0.75f, 1.00f, 1.0f }; /* Selected border */
static const ImVec4 COL_STRIPE       = { 0.08f, 0.08f, 0.20f, 1.0f }; /* Alternating row */

/* my font scaler */
static const float FONTSCALE   = 3.0f;


/* Joystick thresholds */
static const float  JOY_AXIS_DEAD    = 0.3f;   /* Dead zone for analog axis */
static const Uint32 JOY_REPEAT_MS    = 180;    /* Auto-repeat delay for held directions */

/* Screenshot display area size */
static const float  SHOT_W           = 480.0f;
static const float  SHOT_H           = 320.0f;

/* =========================================================================
 * Screenshot texture cache
 * Loaded on demand, one per entry. Never reloaded unless the app restarts.
 * ========================================================================= */
struct ScreenshotTex
{
    SDL_Texture* tex;  /* NULL if not yet loaded or load failed */
    int          w;
    int          h;
};

/* =========================================================================
 * AppState - top-level application state
 * ========================================================================= */
struct AppState
{
    std::vector<MenuEntry>      entries;        /* Loaded config entries */
    std::vector<ScreenshotTex>  textures;       /* Parallel array to entries */

    int     selected;       /* Currently highlighted menu index */
    float   animTime;       /* Accumulated time for animations (seconds) */

    /* Joystick navigation state */
    bool    joyAxisHeld;    /* Is the axis currently deflected? */
    int     joyAxisDir;     /* -1 = up, +1 = down */
    Uint32  joyRepeatNext;  /* SDL_GetTicks() value when next repeat fires */

    /* Status message shown at bottom of screen */
    std::string statusMsg;

    SDL_Joystick* joystick; /* NULL if none found */
};

/* =========================================================================
 * Helper: ImVec4 to ImU32 conversion (for DrawList calls)
 * ========================================================================= */
static ImU32 V4Col(const ImVec4& c)
{
    return ImGui::ColorConvertFloat4ToU32(c);
}

/* =========================================================================
 * LoadScreenshot - Load a PNG from disk into an SDL_Texture.
 * Uses stb_image to decode, then uploads to GPU via SDL_CreateTexture.
 * Returns NULL on failure (missing file, bad PNG, etc.)
 * ========================================================================= */
static SDL_Texture* LoadScreenshot(SDL_Renderer* renderer, const std::string& path)
{
    int w, h, channels;

    /* Force RGBA (4 channels) so we have a consistent pixel format */
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels)
    {
        SDL_Log("stb_image failed to load '%s': %s", path.c_str(), stbi_failure_reason());
        return NULL;
    }

    /* Create an SDL texture from the raw RGBA pixel data */
    SDL_Texture* tex = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC,
        w, h
    );

    if (tex)
    {
        /* pitch = bytes per row = width * 4 bytes (RGBA) */
        SDL_UpdateTexture(tex, NULL, pixels, w * 4);

        /* Enable alpha blending so transparent PNGs render correctly */
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }

    stbi_image_free(pixels);
    return tex;
}

/* =========================================================================
 * EnsureTexture - Load screenshot texture for 'idx' if not already loaded.
 * ========================================================================= */
static void EnsureTexture(AppState& state, SDL_Renderer* renderer, int idx)
{
    if (idx < 0 || idx >= (int)state.textures.size()) return;
    ScreenshotTex& st = state.textures[idx];
    if (st.tex != NULL) return;  /* Already loaded */

    /* Attempt to load from disk */
    const std::string& path = state.entries[idx].screenshotPath;
    st.tex = LoadScreenshot(renderer, path);

    if (st.tex)
    {
        /* Query actual dimensions for aspect-correct rendering */
        SDL_QueryTexture(st.tex, NULL, NULL, &st.w, &st.h);
    }
}

/* =========================================================================
 * DrawLCARSDecorations - Render the static Trek-console chrome:
 *   - Top header bar with rounded left pill and title text
 *   - Left sidebar column (colored blocks stacked vertically)
 *   - Bottom status bar
 *   - Horizontal divider lines
 *
 * All drawn via ImDrawList for pixel-level control.
 * ========================================================================= */
static void DrawLCARSDecorations(ImDrawList* dl,
                                  float screenW, float screenH,
                                  float animTime,
                                  const std::string& statusMsg)
{
    const float HEADER_H    = 70.0f;
    const float SIDEBAR_W   = 140.0f;
    const float FOOTER_H    = 50.0f;
    const float PILL_R      = HEADER_H * 0.5f; /* Radius of the pill end-cap */
    const float PAD         = 8.0f;

    ImU32 colOrange   = V4Col(COL_ORANGE);
    ImU32 colPeach    = V4Col(COL_PEACH);
    ImU32 colLav      = V4Col(COL_LAVENDER);
    ImU32 colBlue     = V4Col(COL_BLUE_LIGHT);
    ImU32 colBlueMid  = V4Col(COL_BLUE_MID);
    ImU32 colPanel    = V4Col(COL_PANEL);
    ImU32 colTextMain = V4Col(COL_TEXT_MAIN);
    ImU32 colTextDim  = V4Col(COL_TEXT_DIM);
    ImU32 colRed      = V4Col(COL_RED);

    /* ---- Header bar ---- */
    /* Left pill (orange half-circle + rectangle body) */
    dl->AddRectFilled(
        { PILL_R, 0.0f },
        { screenW * 0.55f, HEADER_H },
        colOrange
    );
    dl->AddCircleFilled({ PILL_R, HEADER_H * 0.5f }, PILL_R, colOrange);

    /* Peach block to the right of orange */
    dl->AddRectFilled(
        { screenW * 0.55f + PAD, 0.0f },
        { screenW * 0.72f, HEADER_H },
        colPeach
    );

    /* Lavender block far right */
    dl->AddRectFilled(
        { screenW * 0.72f + PAD, 0.0f },
        { screenW, HEADER_H },
        colLav
    );

    /* Header text: system name */
    ImFont* font = ImGui::GetFont();
    float   fontSize = 28.0f*FONTSCALE;
    dl->AddText(font, fontSize,
        { PILL_R * 2.0f + 12.0f, (HEADER_H - fontSize) * 0.5f },
        IM_COL32(10, 10, 30, 255),
        "LCARS ENTERTAINMENT SUBSYSTEM"
    );

    /* Stardate in peach block - calculated from real-time clock */
    char stardate[64];
    {
        time_t    now = time(NULL);
        struct tm lt  = {};
        localtime_s(&lt, &now);

        int   year    = lt.tm_year + 1900;
        bool  isLeap  = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
        int   daysInYr = isLeap ? 366 : 365;

        /* Fractional day including hours/minutes/seconds */
        float dayFrac = (float)lt.tm_yday
                      + (lt.tm_hour * 3600.0f + lt.tm_min * 60.0f + lt.tm_sec) / 86400.0f;

        /* Epoch: stardate 47634.0 = Jan 1 2000, 1000 units per year */
        float sd = 47634.0f + (year - 2000) * 1000.0f + (dayFrac / daysInYr) * 1000.0f;
        snprintf(stardate, sizeof(stardate), "STARDATE %08.1f", sd);
    }
    dl->AddText(font, 16.0f*FONTSCALE,
        { screenW * 0.56f + PAD, (HEADER_H - 16.0f) * 0.5f },
        IM_COL32(20, 20, 50, 255),
        stardate
    );

    /* ---- Left sidebar ---- */
    float sbTop    = HEADER_H + PAD;
    float sbBottom = screenH - FOOTER_H - PAD;
    float sbHeight = sbBottom - sbTop;

    /* Stack of colored blocks with small gaps between them */
    struct SideBlock { ImU32 col; float frac; };
    SideBlock blocks[] =
    {
        { colOrange,  0.18f },
        { colPeach,   0.08f },
        { colLav,     0.22f },
        { colBlueMid, 0.08f },
        { colBlue,    0.28f },
        { colOrange,  0.08f },
        { colPeach,   0.08f },
    };
    int nBlocks = (int)(sizeof(blocks) / sizeof(blocks[0]));

    float y = sbTop;
    for (int i = 0; i < nBlocks; ++i)
    {
        float blockH = sbHeight * blocks[i].frac - PAD;
        /* Rounded left edge on sidebar blocks */
        dl->AddRectFilled(
            { 0.0f, y },
            { SIDEBAR_W, y + blockH },
            blocks[i].col,
            12.0f,   /* rounding */
            ImDrawFlags_RoundCornersRight
        );
        y += blockH + PAD;
    }

    /* Sidebar label text, rotated 90 deg not possible in ImDrawList without
     * a rotated texture. Instead place short horizontal labels. */
    dl->AddText(font, 13.0f*FONTSCALE, { 8.0f, sbTop + 4.0f },
        IM_COL32(10, 10, 30, 255), "GAMING");
    dl->AddText(font, 13.0f*FONTSCALE, { 8.0f, sbTop + sbHeight * 0.26f },
        IM_COL32(10, 10, 30, 255), "EMULATION");
    dl->AddText(font, 13.0f*FONTSCALE, { 8.0f, sbTop + sbHeight * 0.56f },
        IM_COL32(10, 10, 30, 255), "DATABASE");

    /* ---- Blinking alert light in sidebar (pulses red) ---- */
    float pulse = (sinf(animTime * 2.0f) + 1.0f) * 0.5f; /* 0..1 */
    ImVec4 alertCol = {
        COL_RED.x,
        COL_RED.y,
        COL_RED.z,
        0.3f + 0.7f * pulse
    };
    dl->AddCircleFilled(
        { SIDEBAR_W * 0.5f, sbBottom - 24.0f },
        14.0f,
        V4Col(alertCol)
    );
    dl->AddCircle(
        { SIDEBAR_W * 0.5f, sbBottom - 24.0f },
        16.0f,
        colRed, 24, 1.5f
    );

    /* ---- Horizontal divider below header ---- */
    dl->AddLine(
        { SIDEBAR_W + PAD, HEADER_H + 2.0f },
        { screenW, HEADER_H + 2.0f },
        colBlueMid, 2.0f
    );

    /* ---- Footer bar ---- */
    dl->AddRectFilled(
        { 0.0f, screenH - FOOTER_H },
        { screenW, screenH },
        colPanel
    );
    dl->AddLine(
        { 0.0f, screenH - FOOTER_H },
        { screenW, screenH - FOOTER_H },
        colBlueMid, 1.5f
    );

    /* Status message in footer, with pulsing color for activity */
    float statusPulse = (sinf(animTime * 1.5f) + 1.0f) * 0.5f;
    ImVec4 statusCol  = {
        COL_BLUE_LIGHT.x * (0.7f + 0.3f * statusPulse),
        COL_BLUE_LIGHT.y * (0.7f + 0.3f * statusPulse),
        COL_BLUE_LIGHT.z,
        1.0f
    };
    std::string footerText = "[A] SELECT  [UP/DOWN] NAVIGATE  [ESC] QUIT";
    if (!statusMsg.empty())
    {
        footerText = statusMsg;
    }
    dl->AddText(font, 16.0f*FONTSCALE,
        { SIDEBAR_W + 20.0f, screenH - FOOTER_H + (FOOTER_H - 50.0f) * 0.5f },
        V4Col(statusCol),
        footerText.c_str()
    );

    /* Starfleet delta/chevron decorative element in footer right */
    /* Simple approximation using triangles */
    float dx = screenW - 60.0f;
    float dy = screenH - FOOTER_H * 0.5f;
    dl->AddTriangleFilled(
        { dx,        dy - 16.0f },
        { dx - 12.0f, dy + 10.0f },
        { dx + 12.0f, dy + 10.0f },
        colOrange
    );
    dl->AddTriangleFilled(
        { dx,        dy - 10.0f },
        { dx - 7.0f,  dy + 14.0f },
        { dx + 7.0f,  dy + 14.0f },
        colPanel /* cutout to make chevron shape */
    );
}

/* =========================================================================
 * DrawMenuPanel - Render the scrollable entry list on the left content area.
 * Returns the index that was activated (launched), or -1 if none.
 * ========================================================================= */
static int DrawMenuPanel(ImDrawList* dl, AppState& state,
                          float panelX, float panelY,
                          float panelW, float panelH)
{
    int     launched  = -1;
    ImFont* font      = ImGui::GetFont();

    /* Row height must accommodate the scaled font.
     * Font is loaded at 22*FONTSCALE px; add vertical padding around it. */
    const float FONT_SIZE = 22.0f * FONTSCALE;
    const float ROW_PAD   = 10.0f;
    const float itemH     = FONT_SIZE + ROW_PAD;
    const float rowGap    = 6.0f;

    /* Heading label */
    dl->AddText(font, 14.0f * FONTSCALE,
        { panelX + 8.0f, panelY + 4.0f },
        V4Col(COL_TEXT_DIM), "// AVAILABLE PROGRAMS //");

    /* List starts below the heading */
    float listTop = panelY + 14.0f * FONTSCALE + 12.0f;

    /* Mouse click detection directly against row rects - no InvisibleButton
     * needed since we are drawing into the window draw list, not a layout. */
    ImVec2 mousePos   = ImGui::GetIO().MousePos;
    bool   mouseClick = ImGui::GetIO().MouseClicked[0];

    for (int i = 0; i < (int)state.entries.size(); ++i)
    {
        bool   sel = (i == state.selected);
        float  iy  = listTop + i * (itemH + rowGap);

        /* Stop if this row would exceed the panel bounds */
        if (iy + itemH > panelY + panelH) break;

        ImVec2 rMin = { panelX + 2.0f,         iy };
        ImVec2 rMax = { panelX + panelW - 4.0f, iy + itemH };

        /* Mouse click on this row selects and launches */
        if (mouseClick &&
            mousePos.x >= rMin.x && mousePos.x <= rMax.x &&
            mousePos.y >= rMin.y && mousePos.y <= rMax.y)
        {
            state.selected = i;
            launched = i;
        }

        /* Alternating row stripe for unselected rows */
        if (!sel)
        {
            ImVec4 stripe = (i % 2 == 0) ? COL_PANEL : COL_STRIPE;
            dl->AddRectFilled(rMin, rMax, V4Col(stripe), 6.0f);
        }

        /* Selection highlight */
        if (sel)
        {
            dl->AddRectFilled(rMin, rMax, V4Col(COL_SEL_BG), 6.0f);
            dl->AddRect(rMin, rMax, V4Col(COL_SEL_BORDER), 6.0f, 0, 1.5f);

            /* Left accent bar */
            dl->AddRectFilled(
                { rMin.x,        rMin.y + 6.0f },
                { rMin.x + 8.0f, rMax.y - 6.0f },
                V4Col(COL_ORANGE), 3.0f
            );
        }

        /* Vertically center text within the row */
        float textY = iy + (itemH - FONT_SIZE) * 0.5f;

        /* Entry number tag */
        char numTag[8];
        snprintf(numTag, sizeof(numTag), "%02d", i + 1);
        dl->AddText(font, FONT_SIZE,
            { rMin.x + 14.0f, textY },
            sel ? V4Col(COL_ORANGE) : V4Col(COL_TEXT_DIM),
            numTag
        );

        /* Entry title, offset past the number tag */
        dl->AddText(font, FONT_SIZE,
            { rMin.x + 14.0f + FONT_SIZE * 2.2f, textY },
            sel ? V4Col(COL_TEXT_MAIN) : V4Col(COL_TEXT_DIM),
            state.entries[i].name.c_str()
        );
    }

    return launched;
}

/* =========================================================================
 * DrawScreenshotPanel - Render the screenshot for the selected entry.
 * Draws a bordered panel with aspect-correct image scaling.
 * ========================================================================= */
static void DrawScreenshotPanel(ImDrawList* dl, AppState& state,
                                 SDL_Renderer* renderer,
                                 float panelX, float panelY,
                                 float panelW, float panelH)
{
    //ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImFont*     font = ImGui::GetFont();

    /* Panel frame */
    dl->AddRect(
        { panelX, panelY },
        { panelX + panelW, panelY + panelH },
        V4Col(COL_BLUE_MID), 8.0f, 0, 1.5f
    );

    /* Corner accent squares (LCARS decorative element) */
    float cSz = 12.0f;
    dl->AddRectFilled({ panelX,              panelY               }, { panelX + cSz,        panelY + cSz        }, V4Col(COL_ORANGE));
    dl->AddRectFilled({ panelX + panelW-cSz, panelY               }, { panelX + panelW,      panelY + cSz        }, V4Col(COL_PEACH));
    dl->AddRectFilled({ panelX,              panelY + panelH-cSz  }, { panelX + cSz,        panelY + panelH      }, V4Col(COL_LAVENDER));
    dl->AddRectFilled({ panelX + panelW-cSz, panelY + panelH-cSz  }, { panelX + panelW,      panelY + panelH      }, V4Col(COL_BLUE_LIGHT));

    int sel = state.selected;

    if (sel >= 0 && sel < (int)state.entries.size())
    {
        /* Make sure the texture for the selected entry is loaded */
        EnsureTexture(state, renderer, sel);

        ScreenshotTex& st = state.textures[sel];

        /* Label above image */
        std::string label = "// " + state.entries[sel].name + " //";
        dl->AddText(font, 15.0f*FONTSCALE,
            { panelX + 18.0f, panelY + 16.0f },
            V4Col(COL_BLUE_LIGHT),
            label.c_str()
        );

        float imgAreaOff  = FONTSCALE * 15.0f;
        float imgAreaTop  = panelY + 40.0f + imgAreaOff;
        float imgAreaH    = panelH - 56.0f - imgAreaOff;
        float imgAreaW    = panelW - 24.0f;

        if (st.tex)
        {
            /* Scale image to fit panel, preserving aspect ratio */
            float srcAspect = (float)st.w / (float)st.h;
            float dstW, dstH;

            if (imgAreaW / imgAreaH > srcAspect)
            {
                /* Panel is wider than image aspect: fit by height */
                dstH = imgAreaH;
                dstW = dstH * srcAspect;
            }
            else
            {
                /* Panel is taller than image aspect: fit by width */
                dstW = imgAreaW;
                dstH = dstW / srcAspect;
            }

            /* Center the image in the panel area */
            float ix = panelX + 12.0f + (imgAreaW - dstW) * 0.5f;
            float iy = imgAreaTop + (imgAreaH - dstH) * 0.5f;

            dl->AddImage(
                (ImTextureID)(intptr_t)st.tex,
                { ix, iy },
                { ix + dstW, iy + dstH }
            );

            /* Subtle vignette overlay (darkened border over the image) */
            dl->AddRect({ ix, iy }, { ix+dstW, iy+dstH },
                IM_COL32(100, 150, 255, 60), 0.0f, 0, 6.0f);
        }
        else
        {
            /* No image loaded: show a placeholder message */
            const char* noImg = "[ NO IMAGE ]";
            ImVec2 ts = ImGui::CalcTextSize(noImg);
            dl->AddText(font, 18.0f*FONTSCALE,
                { panelX + (panelW - ts.x) * 0.5f,
                  imgAreaTop + (imgAreaH - 18.0f) * 0.5f },
                V4Col(COL_TEXT_DIM),
                noImg
            );
        }
    }
}

/* =========================================================================
 * HandleJoystick - Poll joystick state, map to navigation events.
 * Updates state.selected based on axis or hat movement.
 * Returns true if the user pressed the confirm/launch button.
 *
 * Uses a simple repeat scheme: first movement fires immediately, then
 * repeats every JOY_REPEAT_MS while held.
 * ========================================================================= */
static bool HandleJoystick(AppState& state, SDL_Event& ev)
{
    int numEntries = (int)state.entries.size();
    if (numEntries == 0) return false;

    bool launch = false;

    if (ev.type == SDL_JOYBUTTONDOWN)
    {
        /* Button 0 = confirm/launch (A on Xbox, Cross on PlayStation) */
        if (ev.jbutton.button == 0)
        {
            launch = true;
        }
    }
    else if (ev.type == SDL_JOYAXISMOTION)
    {
        /* Axis 1 = left stick Y on most controllers */
        if (ev.jaxis.axis == 1)
        {
            float val = ev.jaxis.value / 32767.0f;

            if (fabsf(val) > JOY_AXIS_DEAD)
            {
                int dir = (val < 0.0f) ? -1 : 1;

                if (!state.joyAxisHeld)
                {
                    /* First movement: apply immediately */
                    state.selected = (state.selected + dir + numEntries) % numEntries;
                    state.joyAxisHeld    = true;
                    state.joyAxisDir     = dir;
                    state.joyRepeatNext  = SDL_GetTicks() + JOY_REPEAT_MS * 3; /* longer initial delay */
                }
                else if (dir == state.joyAxisDir)
                {
                    /* Auto-repeat while held */
                    if (SDL_GetTicks() >= state.joyRepeatNext)
                    {
                        state.selected = (state.selected + dir + numEntries) % numEntries;
                        state.joyRepeatNext = SDL_GetTicks() + JOY_REPEAT_MS;
                    }
                }
            }
            else
            {
                state.joyAxisHeld = false;
            }
        }
    }
    else if (ev.type == SDL_JOYHATMOTION)
    {
        /* D-pad */
        if (ev.jhat.value & SDL_HAT_UP)
        {
            state.selected = (state.selected - 1 + numEntries) % numEntries;
        }
        else if (ev.jhat.value & SDL_HAT_DOWN)
        {
            state.selected = (state.selected + 1) % numEntries;
        }
    }

    return launch;
}

/* =========================================================================
 * SetupImGuiStyle - Apply Trek console color theme to Dear ImGui.
 * ========================================================================= */
static void SetupImGuiStyle()
{
    ImGuiStyle& st = ImGui::GetStyle();

    st.WindowRounding    = 0.0f;
    st.ChildRounding     = 6.0f;
    st.FrameRounding     = 4.0f;
    st.PopupRounding     = 4.0f;
    st.ScrollbarRounding = 4.0f;
    st.GrabRounding      = 4.0f;
    st.WindowBorderSize  = 0.0f;
    st.FrameBorderSize   = 0.0f;
    st.WindowPadding     = { 0.0f, 0.0f };
    st.FramePadding      = { 4.0f, 4.0f };
    st.ItemSpacing       = { 4.0f, 4.0f };

    ImVec4* colors = st.Colors;

    /* Wipe to dark blue base */
    for (int i = 0; i < ImGuiCol_COUNT; ++i)
        colors[i] = { 0.04f, 0.04f, 0.14f, 1.0f };

    colors[ImGuiCol_WindowBg]        = COL_BG;
    colors[ImGuiCol_ChildBg]         = COL_PANEL;
    colors[ImGuiCol_Text]            = COL_TEXT_MAIN;
    colors[ImGuiCol_TextDisabled]    = COL_TEXT_DIM;
    colors[ImGuiCol_Border]          = COL_BLUE_MID;
    colors[ImGuiCol_FrameBg]         = COL_PANEL;
    colors[ImGuiCol_FrameBgHovered]  = COL_SEL_BG;
    colors[ImGuiCol_FrameBgActive]   = COL_SEL_BG;
    colors[ImGuiCol_ScrollbarBg]     = COL_PANEL;
    colors[ImGuiCol_ScrollbarGrab]   = COL_BLUE_MID;
    colors[ImGuiCol_Button]          = COL_BLUE_MID;
    colors[ImGuiCol_ButtonHovered]   = COL_BLUE_LIGHT;
    colors[ImGuiCol_ButtonActive]    = COL_ORANGE;
    colors[ImGuiCol_Header]          = COL_SEL_BG;
    colors[ImGuiCol_HeaderHovered]   = COL_SEL_BG;
    colors[ImGuiCol_HeaderActive]    = COL_SEL_BG;
    colors[ImGuiCol_Separator]       = COL_BLUE_MID;
    colors[ImGuiCol_PopupBg]         = COL_PANEL;
}

/* =========================================================================
 * main - Entry point
 * ========================================================================= */
int main(int argc, char* argv[])
{
    /* ---- Determine config path (optional first arg overrides default) ---- */
    const char* configPath = CONFIG_PATH;
    if (argc > 1) configPath = argv[1];

    /* ---- Load configuration ---- */
    AppState state = {};
    state.selected = 0;

    std::string configError;
    if (!LoadConfig(configPath, state.entries, configError))
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
            "Config Error", configError.c_str(), NULL);
        return 1;
    }

    if (state.entries.empty())
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
            "Config Error",
            "No valid entries found in config file.\n"
            "Check that each [Entry] block has name, launch, and screenshot fields.",
            NULL);
        return 1;
    }

    /* Allocate parallel texture array, initially all NULL */
    state.textures.resize(state.entries.size(), { NULL, 0, 0 });

    /* ---- SDL2 init ---- */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
            "SDL Init Error", SDL_GetError(), NULL);
        return 1;
    }

    /* Open first available joystick if any */
    state.joystick = NULL;
    if (SDL_NumJoysticks() > 0)
    {
        state.joystick = SDL_JoystickOpen(0);
        if (!state.joystick)
        {
            SDL_Log("Warning: could not open joystick 0: %s", SDL_GetError());
        }
    }

    /* Create fullscreen window */
    SDL_Window* window = SDL_CreateWindow(
        "LCARS Entertainment Subsystem",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1920, 1080,
        SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!window)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
            "Window Error", SDL_GetError(), NULL);
        SDL_Quit();
        return 1;
    }

    /* Hide the system cursor — we don't need it (joystick UI) */
    SDL_ShowCursor(SDL_DISABLE);

    /* Create hardware-accelerated renderer */
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (!renderer)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
            "Renderer Error", SDL_GetError(), NULL);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* ---- Dear ImGui init ---- */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = NULL;  /* Suppress imgui.ini file creation */

    SetupImGuiStyle();

    /* Load a slightly larger default font for readability on big screens */
    io.Fonts->AddFontFromFileTTF("Okuda.otf", 66.0f);

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    /* ---- Pre-load screenshot for initial selection ---- */
    EnsureTexture(state, renderer, state.selected);

    /* ---- Main loop ---- */
    bool quit         = false;
    Uint32 lastTick   = SDL_GetTicks();

    while (!quit)
    {
        /* Delta time for animations */
        Uint32 nowTick = SDL_GetTicks();
        float  dt      = (nowTick - lastTick) / 1000.0f;
        lastTick       = nowTick;
        state.animTime += dt;

        /* ---- Event processing ---- */
        int prevSelected = state.selected;
        int launchIdx    = -1;

        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            ImGui_ImplSDL2_ProcessEvent(&ev);

            switch (ev.type)
            {
                case SDL_QUIT:
                    quit = true;
                    break;

                case SDL_KEYDOWN:
                    switch (ev.key.keysym.sym)
                    {
                        case SDLK_ESCAPE:
                            quit = true;
                            break;

                        case SDLK_UP:
                            if (state.entries.size() > 0)
                                state.selected = (state.selected - 1 + (int)state.entries.size())
                                                 % (int)state.entries.size();
                            break;

                        case SDLK_DOWN:
                            if (state.entries.size() > 0)
                                state.selected = (state.selected + 1)
                                                 % (int)state.entries.size();
                            break;

                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                            launchIdx = state.selected;
                            break;

                        default:
                            break;
                    }
                    break;

                case SDL_JOYBUTTONDOWN:
                case SDL_JOYAXISMOTION:
                case SDL_JOYHATMOTION:
                    if (HandleJoystick(state, ev))
                    {
                        launchIdx = state.selected;
                    }
                    break;

                default:
                    break;
            }
        }

        /* If selection changed, pre-load the new screenshot */
        if (state.selected != prevSelected)
        {
            EnsureTexture(state, renderer, state.selected);
        }

        /* ---- Launch selected entry ---- */
        if (launchIdx >= 0 && launchIdx < (int)state.entries.size())
        {
            const MenuEntry& entry = state.entries[launchIdx];

            /* Notify user */
            state.statusMsg = "LAUNCHING: " + entry.name + "...";

            /* Render one frame with the status message before blocking */
            /* (minimal render to show the launching message) */
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            int sw, sh;
            SDL_GetWindowSize(window, &sw, &sh);

            /* Fullscreen overlay window */
            ImGui::SetNextWindowPos({ 0, 0 });
            ImGui::SetNextWindowSize({ (float)sw, (float)sh });
            ImGui::Begin("##overlay_launch", NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoDecoration);
            ImGui::End();

            ImGui::Render();
            SDL_SetRenderDrawColor(renderer, 5, 5, 20, 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
            SDL_RenderPresent(renderer);

            /* Temporarily suspend ImGui/SDL rendering and launch child */
            std::string launchError;
            bool ok = LaunchAndWait(entry.launchString, launchError);

            if (!ok)
            {
                /* Show error via message box (child failed to start) */
                //SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                //    "Launch Failed", launchError.c_str(), window);
                state.statusMsg = "LAUNCH FAILED: " + launchError;
            }
            else
            {
                state.statusMsg = "PROGRAM EXITED. SYSTEM NOMINAL.";
            }

            /* Force window focus back to us after child exits */
            SDL_RaiseWindow(window);
        }

        /* ---- Render frame ---- */
        int screenW, screenH;
        SDL_GetWindowSize(window, &screenW, &screenH);
        float sw = (float)screenW;
        float sh = (float)screenH;

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        /* -- Fullscreen background window -- */
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize({ sw, sh });
        ImGui::SetNextWindowBgAlpha(1.0f);
        ImGui::Begin("##main", NULL,
            ImGuiWindowFlags_NoTitleBar      |
            ImGuiWindowFlags_NoResize        |
            ImGuiWindowFlags_NoMove          |
            ImGuiWindowFlags_NoScrollbar     |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoDecoration);

        /* Draw the LCARS chrome into the background window's draw list */
        ImDrawList* dl = ImGui::GetWindowDrawList();
        DrawLCARSDecorations(dl, sw, sh, state.animTime, state.statusMsg);

        /* Layout constants (must match DrawLCARSDecorations) */
        const float HEADER_H  = 70.0f;
        const float SIDEBAR_W = 140.0f;
        const float FOOTER_H  = 50.0f;
        const float PAD       = 8.0f;

        float contentLeft = SIDEBAR_W + PAD * 2.0f;
        float contentTop  = HEADER_H + PAD * 2.0f;
        float contentW    = sw - contentLeft - PAD;
        float contentH    = sh - HEADER_H - FOOTER_H - PAD * 3.0f;

        /* Left portion: menu list (40% of content width) */
        float menuW = contentW * 0.40f;
        float menuH = contentH;

        /* Right portion: screenshot (remaining width) */
        float shotX = contentLeft + menuW + PAD;
        float shotW = contentW - menuW - PAD * 2.0f;
        float shotH = contentH;

        /* Draw menu - check for mouse-click launch */
        int menuLaunch = DrawMenuPanel(dl, state,
            contentLeft, contentTop, menuW, menuH);

        if (menuLaunch >= 0)
        {
            launchIdx = menuLaunch;
            /* Will be processed next frame (already handled this frame) */
            /* Re-set so the launch block fires on next iteration.
             * Easiest: set statusMsg and let the next iteration handle it.
             * Actually we can just re-trigger here since we haven't launched yet. */
        }

        DrawScreenshotPanel(dl, state, renderer, shotX, contentTop, shotW, shotH);

        /* -- Render -- */
        ImGui::End();   /* ##main must be closed before Render() */
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 5, 5, 20, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    /* ---- Cleanup ---- */
    /* Free all loaded textures */
    for (auto& st : state.textures)
    {
        if (st.tex)
        {
            SDL_DestroyTexture(st.tex);
        }
    }

    if (state.joystick)
    {
        SDL_JoystickClose(state.joystick);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
