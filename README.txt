EMULATOR FRONTEND - BUILD INSTRUCTIONS
=======================================

DEPENDENCIES (all free, no installers required)
-------------------------------------------------

1. SDL2 Development Libraries (MSVC)
   https://github.com/libsdl-org/SDL/releases
   Download: SDL2-devel-2.x.x-VC.zip
   Extract and place the SDL2 folder at: emufrontend\SDL2\
   Expected layout:
     SDL2\include\SDL.h  (etc)
     SDL2\lib\x64\SDL2.lib
     SDL2\lib\x64\SDL2main.lib
     SDL2\lib\x64\SDL2.dll

2. Dear ImGui
   https://github.com/ocornut/imgui
   Download the repository (or clone it).
   Copy these files into emufrontend\imgui\:
     imgui.h
     imgui.cpp
     imgui_internal.h
     imgui_draw.cpp
     imgui_tables.cpp
     imgui_widgets.cpp
     backends\imgui_impl_sdl2.h
     backends\imgui_impl_sdl2.cpp
     backends\imgui_impl_sdlrenderer2.h
     backends\imgui_impl_sdlrenderer2.cpp

3. stb_image.h (single header)
   https://github.com/nothings/stb/blob/master/stb_image.h
   Copy stb_image.h into emufrontend\ (same folder as main.cpp)

DIRECTORY LAYOUT AFTER SETUP
------------------------------
  emufrontend\
    EmuFrontend.vcxproj
    frontend.cfg
    main.cpp
    config.cpp
    config.h
    launcher.cpp
    launcher.h
    stb_image.h
    imgui\
      imgui.h
      imgui.cpp
      imgui_internal.h
      imgui_draw.cpp
      imgui_tables.cpp
      imgui_widgets.cpp
      imgui_impl_sdl2.h
      imgui_impl_sdl2.cpp
      imgui_impl_sdlrenderer2.h
      imgui_impl_sdlrenderer2.cpp
    SDL2\
      include\  (SDL.h, etc)
      lib\x64\
        SDL2.lib
        SDL2main.lib
        SDL2.dll

BUILD
------
1. Open EmuFrontend.vcxproj in Visual Studio 2022.
2. Select x64 / Release (or Debug).
3. Build Solution (F7).
4. Copy SDL2\lib\x64\SDL2.dll next to the built .exe
   (typically x64\Release\EmuFrontend.exe).

OPTIONAL: LCARS FONT
---------------------
The default ImGui font is clean but not very Trek-like.
For a more authentic look:
1. Download "AmazDooMLeft" or "Okuda" font (free for personal use,
   search "LCARS font ttf").
2. Place the .ttf in the same folder as the exe.
3. In main.cpp, replace:
     io.Fonts->AddFontDefault();
   with:
     io.Fonts->AddFontFromFileTTF("AmazDooMLeft.ttf", 22.0f);

RUNTIME
--------
- Place frontend.cfg in the same directory as the .exe.
- Edit frontend.cfg to point to your emulators and screenshots.
- The exe can be launched with an alternate config path as argument:
    EmuFrontend.exe C:\path\to\my.cfg

CONTROLS
---------
  Joystick axis 1 (Y) / D-pad Up/Down  = Navigate menu
  Button 0 (A / Cross)                 = Launch selected entry
  Keyboard Up/Down arrows              = Navigate (fallback)
  Keyboard Enter                       = Launch (fallback)
  Keyboard Escape                      = Quit

CONFIG FORMAT
--------------
  # This is a comment
  ; This is also a comment

  [Entry]
  name       = Display Name Here
  launch     = C:\path\to\emulator.exe arg1 arg2
  screenshot = C:\path\to\image.png

  [Entry]
  name       = Another Title
  launch     = C:\emu\mame64.exe romname -nowindow
  screenshot = C:\screens\romname.png

  Rules:
  - Each [Entry] block must contain all three fields.
  - Incomplete entries are silently skipped.
  - Maximum 16 entries.
  - Paths with spaces do not need quotes in the config
    (the launch string is passed directly to CreateProcess).
