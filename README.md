# Archimedes

A lightweight, cross-platform C game development framework built on SDL2 that provides essential 2D graphics, audio, input handling, and UI widget functionality.

## Build
To build natively
```
make native
./bin/native
```

To build the shared object file also install/uninstall them from usr/lib/ and usr/include
```
make shared

make install
make uninstall
```

To build Archimedes directly for Emscripten this will build a index/index.js
that can be use almost anywhere on the web.
*This isn't how you're supposed to use Archimedes, but it's a good way to test if everything is setup right*
```
make EM
python3 -m http.server 8000 //you can go to localhost:8000/index for a basic display
```

To build static archive for Emscripten use
*this is installed in your projects lib/*
```
make EMARCH
```

## Overview

Archimedes is a minimalist game development library written in C that abstracts common SDL2 operations into a clean, easy-to-use API. The framework is designed for developers who want to create 2D games and applications without dealing with the complexity of raw SDL2 programming. It supports both native compilation and web deployment through Emscripten, making it perfect for cross-platform game development.

The library follows a modular architecture with separate components for graphics rendering, audio playback, input management, text rendering, image handling, UI widgets, layout, animations, and more. All functionality is exposed through a single header file (`Archimedes.h`) and can be easily integrated into existing projects or used as the foundation for new games.

## Features

### Graphics & Rendering
- **Primitive Drawing**: Points, lines, circles, triangles, and rectangles with filled variants
- **Image Support**: PNG image loading with caching system for optimized memory usage
- **Texture Management**: Automatic texture loading and management with SDL2 backend
- **Scene Management**: Simplified scene preparation and presentation workflow
- **Viewport/Camera**: Pan and zoom support for 2D camera control

### Audio System
- **Sound Effects**: Load and play sound effects with channel-based mixing via SDL2_mixer
- **Background Music**: Stream music with looping, fade-in/out, pause/resume
- **Channel Management**: Reserved and auto-allocated channels for organized audio
- **Volume Control**: Per-channel and master volume, independent music volume

### Input Handling
- **Keyboard Input**: Full keyboard state tracking with 350+ key support
- **Mouse Support**: Complete mouse input including position, buttons, and scroll wheel
- **Event Processing**: Centralized input processing with easy state queries
- **Cross-platform**: Consistent input handling across different operating systems

### Text & UI
- **Font Rendering**: TTF font support with glyph atlas rendering
- **Styled Text**: `aTextStyle_t` configuration for color, alignment, scale, wrapping, and padding
- **UI Widgets**: Button, slider, input field, select dropdown, container, and control widgets
- **Widget System**: AUF-based widget configuration with event handling
- **FlexBox Layout**: CSS-inspired automatic layout engine with direction, justify, align, gap, and padding

### Animations & Sprites
- **Sprite Sheets**: Load and extract individual sprites from sprite sheet images
- **Animations**: Timer-driven frame animation system

### World & Tiles
- **2D World**: Tile-based 2D world creation
- **Tile Bitmask**: Bitmask-based auto-tiling for seamless tile transitions

### Core Systems
- **Delta Time**: Frame-rate independent timing system for smooth animations
- **Timers**: Create, start, stop, pause, and oneshot timer support
- **Cross-platform**: Native compilation for desktop and web deployment via Emscripten
- **Memory Management**: Efficient caching systems for images and textures
- **Error Handling**: Logging system with multiple severity levels (NORMAL, WARNING, FATAL, INFO, DEBUG, TRACE)

## Quick Start

### Minimal Example

```c
#include <Archimedes.h>

int playerX = 400, playerY = 300;

void logic(float dt) {
    a_DoInput();

    if (app.keyboard[SDL_SCANCODE_W]) playerY -= 5;
    if (app.keyboard[SDL_SCANCODE_S]) playerY += 5;
    if (app.keyboard[SDL_SCANCODE_A]) playerX -= 5;
    if (app.keyboard[SDL_SCANCODE_D]) playerX += 5;

    if (app.keyboard[SDL_SCANCODE_ESCAPE]) app.running = 0;
}

void render(float dt) {
    a_DrawFilledCircle(playerX, playerY, 20, blue);

    aTextStyle_t style = {
        .type = FONT_GAME,
        .fg = {255, 255, 255, 255},
        .align = TEXT_ALIGN_LEFT,
        .scale = 1.0f
    };
    a_DrawText("Use WASD to move", 10, 10, style);
}

int main(void) {
    a_Init(SCREEN_WIDTH, SCREEN_HEIGHT, "My Game");

    app.delegate.logic = logic;
    app.delegate.draw = render;

    while (app.running) {
        a_PrepareScene();
        app.delegate.logic(a_GetDeltaTime());
        app.delegate.draw(a_GetDeltaTime());
        a_PresentScene();
    }

    a_Quit();
    return 0;
}
```

### Compile and Run

```bash
# Install the library
make shared
sudo make install

# Compile your game
gcc main.c -o game -lArchimedes -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lm

# Run
./game
```

## Documentation

Comprehensive usage guides available in the [Wiki](https://github.com/McCoy1701/Archimedes/wiki):

### Core Modules
- [Initialization](https://github.com/McCoy1701/Archimedes/wiki/Usage-Initialize) - Framework setup and lifecycle
- [Drawing](https://github.com/McCoy1701/Archimedes/wiki/Usage-Drawing) - Graphics primitives and rendering
- [Input](https://github.com/McCoy1701/Archimedes/wiki/Usage-Input) - Keyboard and mouse handling
- [Text](https://github.com/McCoy1701/Archimedes/wiki/Usage-Text) - Font rendering and text display
- [Delta Time](https://github.com/McCoy1701/Archimedes/wiki/Usage-Delta-Time) - Frame-rate independent timing

### Utility Modules
- [Image](https://github.com/McCoy1701/Archimedes/wiki/Usage-Image) - PNG loading and caching
- [Audio](https://github.com/McCoy1701/Archimedes/wiki/Usage-Audio) - Sound effect and music playback
