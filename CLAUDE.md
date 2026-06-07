# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-C6 firmware for controlling 4 relays, built with the ESP-IDF framework (v6.0.1). The project targets `esp32c6` and uses the standard ESP-IDF CMake build system on top of FreeRTOS.

## Build & Flash Commands

All commands require the ESP-IDF environment to be sourced (`source $IDF_PATH/export.sh`), which is done automatically inside the devcontainer.

```bash
# Configure target (run once)
idf.py set-target esp32c6

# Build
idf.py build

# Flash to device (port: /dev/ttyACM0)
idf.py -p /dev/ttyACM0 flash

# Flash and open serial monitor
idf.py -p /dev/ttyACM0 flash monitor

# Serial monitor only
idf.py -p /dev/ttyACM0 monitor

# Open menuconfig (SDK/project configuration)
idf.py menuconfig

# Clean build artifacts
idf.py fullclean
```

Exit the serial monitor with `Ctrl+]`.

## Development Environment

The project includes a devcontainer (`.devcontainer/`) based on `espressif/idf` Docker image. Open in VSCode and **Reopen in Container** to get a fully configured ESP-IDF environment with the ESP-IDF VSCode extension.

For local (non-container) development, `idf.currentSetup` points to `~/.espressif/v6.0.1/esp-idf` and clangd is configured to use `build/compile_commands.json` (generated after first build).

## Code Architecture

```
main/main.c   — firmware entry point: app_main() runs on FreeRTOS after boot
```

ESP-IDF's `app_main()` replaces a traditional `main()`. It runs in a FreeRTOS task; use `xTaskCreate` to spawn additional tasks. GPIO control for relays is done via the `driver/gpio.h` API.

## Code Style

Follows [Linux kernel C style](https://www.kernel.org/doc/html/latest/process/coding-style.html):

- **Indentation**: tabs, tab width = 8
- **Line length**: 80 characters
- **Braces**: K&R — opening brace at end of line, except for function definitions (brace on its own line)
- **Naming**: `snake_case` for functions/variables, `ALL_CAPS` for macros and constants; task functions use `_task` suffix
- **Comments**: `/* */` block style; `//` only for short inline notes
- **No typedef for structs** — use `struct foo` directly unless it's a function pointer or opaque handle

## Debugging

OpenOCD config: `board/esp32c6-builtin.cfg` (uses the built-in USB JTAG). The VSCode launch config (`.vscode/launch.json`) attaches via Eclipse CDT GDB adapter.
