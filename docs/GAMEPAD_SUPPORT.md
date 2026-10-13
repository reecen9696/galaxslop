# Gamepad Support in GalaxyQuest Web Build

## Overview

The web build **already has gamepad support** implemented using Emscripten's native gamepad API. This document explains the current implementation, mapping, and integration.

## Implementation Location

- **File**: `platform/src/web/present_web.cpp`
- **Function**: `web_read_input()` and `readGamepad()`
- **Build System**: Compiled into `smg.wasm` for browser deployment

## Current Gamepad Mapping (Standard XBox Controller)

### Wii Remote (Left Joycon)
| Gamepad Button | Wii Remote Bit |
|---------------|----------------|
| `face0` (A)   | `kA` (0x0800)  |
| `face1` (B)   | `kB` (0x0400)  |
| `back` (Z)    | `kZ` (0x2000)  |
| `start`       | `kC` (0x4000)  |
| `select`      | `kMinus` (0x1000)|
| `btn12` (Up)  | `kUp`          |
| `btn13` (Down)| `kDown`        |
| `btn14` (Left)| `kLeft`        |
| `btn15` (Right)| `kRight`      |
| `btn16`       | `kHome`        |
| `bp11` (Plus) | `kPlus`        |

### Nunchuk (Right Joycon - Stick & C/Z)
- **Stick**: `axes[0]`, `axes[1]` → mapped to `stickX`, `stickY` (-1..1, right/up positive)
- **C Button**: `digitalButton[3]` → maps to Wii remote button context
- **Z Button**: `digitalButton[2]` → mapped to `kZ`

### Pointer Control (Right Stick)
- When the right stick moves beyond dead zone (0.15), it drives a virtual cursor
- Cursor speed: 0.03 screen half-widths per frame at full tilt
- Once active, pointer follows the stick until mouse movement
- Implemented in `readGamepad()`, lines 258-294

### Accelerometer (Gyroscope)
- **Trigger**: Dual analog triggers (axes 4,5 on XBox gamepads)
- Alternates acceleration direction each frame: ±2.5 units
- Simulates remote shake for camera control

## Emscripten API Usage

The implementation uses these Emscripten functions:

```cpp
emscripten_sample_gamepad_data()  // Sample all connected gamepads
emscripten_get_num_gamepads()      // Get count of connected controllers
emscripten_get_gamepad_status(i, &g)  // Get detailed state for controller i
```

## Integration with Input Pipeline

```cpp
void web_read_input(PortPadState* pad) {
    memset(pad, 0, sizeof(*pad));
    pad->connected = 1;
    
    // 1. Keyboard/mouse fallback (always active)
    pad->stickX = (held("KeyD") ? 1.0f : 0.0f) - (held("KeyA") ? 1.0f : 0.0f);
    pad->stickY = (held("KeyW") ? 1.0f : 0.0f) - (held("KeyS") ? 1.0f : 0.0f);
    
    // 2. Gamepad input (Emscripten API)
    readGamepad(pad);
    
    // 3. Touch overlay (if enabled on mobile)
    readTouch(pad);
}
```

## Multi-Input Priority

The `web_read_input()` function combines three input sources:

1. **Keyboard** - Primary fallback (WASD movement, Space=Action)
2. **Gamepad** - Standard XBox mapping via Emscripten
3. **Touch overlay** - Mobile web controls (when enabled)

All three are read once per frame and merged into a single `PortPadState`.

## Build Integration

The gamepad support is automatically included in:

- **Development build**: When running with `tools/web/smoke.mjs`
- **Production build**: Included in `smg.wasm` when built with Emscripten
- No additional configuration required - uses Emscripten's built-in detection

## Platform Compatibility

| Platform | Gamepad Support | Notes |
|----------|-----------------|-------|
| Desktop (Chrome/Firefox/Edge) | ✅ Full | All buttons, axes, gyroscope |
| Mobile Android | ✅ Full | Works with connected Bluetooth gamepads |
| iOS Safari | ⚠️ Partial | Limited haptics support |
| Touch screens | ✅ Touch overlay | Separate touch.js handles mobile input |

## Testing

Gamepad support can be tested by:

1. Navigate to the web app at `http://localhost:<port>`
2. Connect an XBox-style controller via USB or Bluetooth
3. The controller should appear in the browser's devtools (Performance → Input)
4. Check that button presses and stick movement affect game input

## Future Enhancements

Potential improvements that could be added:

1. **Configuration UI** - Allow users to remap buttons in Settings
2. **Multiple controller support** - Handle up to 4 simultaneous controllers
3. **Controller pairing mode** - Button combination to switch between paired devices
4. **Dead zone configuration** - Per-gamepad sensitivity settings
5. **Rumble profiles** - Configure haptic feedback intensity

## Related Files

- `platform/src/web/present_web.cpp` - Main gamepad implementation
- `platform/src/web/present_web.h` - Declaration
- `web/touch.js` - Touch overlay controls (alternative input method)
- `platform/include/port/input.h` - Input state structure definition

## References

- Emscripten Gamepad API: https://emscripten.org/docs/api_reference/html5.html#emscripten_get_gamepad_status
- Build spec section 7: Web build controls and input handling
- BUILD_SPEC.md for complete web build specifications
