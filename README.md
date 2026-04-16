# LSP-ReShade — ReShade Input Passthrough

Enables mouse and keyboard interaction with ReShade overlays while Lossless Scaling is running. Without this addon, Lossless Scaling's fullscreen window captures all input and prevents you from clicking or typing in the ReShade UI.

## What It Does

When activated via hotkey, the addon:

1. Detects the Lossless Scaling window and the target game window behind it
2. Makes Lossless Scaling's windows transparent to input (click-through)
3. Allows mouse clicks and keyboard input to reach the ReShade overlay
4. Optionally performs an auto-click and hotkey re-press sequence to properly open ReShade's menu

Press the hotkey again to deactivate and return to normal operation. The addon automatically deactivates if the target window loses focus.

## Installation

### Build

```bash
cd Addons/LSP-ReShade
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### Deploy

1. Create a folder: `<Lossless Scaling>/addons/LSP-ReShade/`
2. Copy `LSP_ReShade.dll` and `addon.json` into that folder
3. Restart Lossless Scaling

## Configuration

Open the LosslessProxy overlay and navigate to the LSP-ReShade settings tab.

| Setting | Default | Description |
|---------|---------|-------------|
| Key | Home | The primary hotkey to toggle input passthrough |
| Ctrl / Alt / Shift | Off | Modifier keys for the hotkey combo |
| Auto Click & Repress | On | Automatically clicks and re-presses the hotkey to properly trigger ReShade's menu toggle |

Settings are persisted in `addons/config.json` and restored on next launch.

## How It Works

- **Worker thread** runs at 10-50ms intervals monitoring the hotkey and window focus
- **Window enumeration** identifies Lossless Scaling's windows and the game window beneath them
- **Input simulation** uses `SendInput` to forward clicks and key combos to the target
- **WS_EX_TRANSPARENT** flag is toggled on LS windows to enable click-through
- **Focus validation** checks that either the LS window or the target game is in the foreground before toggling

## Requirements

- LosslessProxy v0.3.0+
- ReShade installed on the target game





## ⚠️ Disclaimer

This add-on is an unofficial extension for Lossless Scaling.

It is NOT affiliated with, endorsed by, or supported by
Lossless Scaling Developers in any way.

This add-on was developed through independent analysis and
reverse engineering of the software behavior. No proprietary
source code or assets are included.

Use at your own risk.

The author assumes no responsibility for any damage, data loss,
account bans, or other consequences resulting from the use of
this add-on.

This add-on may interact with the software at runtime in
non-documented ways.


## Legal Notice / Trademarks

All trademarks, product names, and company names are the property
of their respective owners and are used for identification purposes only.

