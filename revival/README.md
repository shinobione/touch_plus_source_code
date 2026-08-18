# TouchPlus Revival — Option B

This directory is the modern revival path for the Ractiv Touch+ hardware.

The goal is **not** to recreate the abandoned 2015 desktop application. The Touch+ is treated as a reusable stereo/IMU sensor and a new runtime is built around it.

## Canonical hardware facts recovered from the original code

- USB VID: `1E4E`
- USB PID: `0107`
- Windows friendly name used by the legacy stack: `Touch+ Camera`
- Expected video transport: UVC/MJPEG
- Expected combined frame: `1280x480`
- Logical stereo split: `640x480` left + `640x480` right
- Original target rate: up to `60 fps`
- Hardware also exposes accelerometer, exposure/gain controls, GPIO/LED control and flash/serial access through vendor-specific controls.

## Architecture

```text
Touch+ hardware
      |
      v
[ Sensor Layer ]  UVC video + USB controls + IMU
      |
      v
[ Stereo Layer ]  calibration + rectification + depth/triangulation
      |
      v
[ Tracking Layer ] modern hand landmarks / object landmarks
      |
      v
[ Fusion Layer ]  left/right association + 3D coordinates + smoothing
      |
      v
[ Runtime API ]   pointer / gestures / OSC / MIDI / WebSocket / Unity
```

The legacy Ractiv algorithms remain valuable as documentation and as a fallback reference, but new code should depend on them only where they provide hardware-specific knowledge that cannot be replaced cleanly.

## Phase 0-preflight — raw Windows USB/PnP probe

A device that fails USB enumeration never reaches Media Foundation and therefore cannot appear in the camera probe. `touchplus_usb_probe.exe` inspects present USB devnodes through SetupAPI/Configuration Manager instead of the video stack.

It reports:

- any visible `VID_1E4E&PID_0107` Touch+ identity;
- USB descriptor-failure / `USB\\UNKNOWN` style nodes;
- Windows PnP problem codes including Code 43;
- instance ID, hardware IDs, location, service and parent device when available.

Run:

```powershell
.\build\revival\Release\touchplus_usb_probe.exe
```

Use `--all` only when the filtered output is not enough:

```powershell
.\build\revival\Release\touchplus_usb_probe.exe --all
```

For an unidentified Code 43 device, perform a correlation test: run the probe with the Touch+ unplugged, then plug it in and run again. If the `[USB-FAIL]` node appears/disappears with the sensor, we have identified the physical Touch+ even though Windows cannot yet read its VID/PID.

## Phase 0A — Windows UVC hardware probe

The camera executable deliberately uses **only Windows Media Foundation and WIC**. It does not depend on OpenCV 2.4, the old Ractiv UI, SFML, or the Etron DLLs.

It must prove four things on the real device:

1. Windows enumerates the Touch+ as `VID_1E4E&PID_0107` (or by the `Touch+ Camera` friendly name).
2. The camera exposes a native `1280x480` video mode.
3. A frame can be captured through the standard Windows video stack.
4. The combined frame can be split into two `640x480` eye images.

Successful capture writes:

```text
touchplus-probe/
  touchplus-full.png
  touchplus-left.png
  touchplus-right.png
```

### Build on Windows

Requirements:

- Windows 10 or Windows 11
- Visual Studio 2022 Build Tools or Visual Studio 2022 with the Desktop C++ workload
- CMake 3.23+

From a Developer PowerShell:

```powershell
cmake -S revival -B build/revival -A x64
cmake --build build/revival --config Release
```

Then connect the Touch+ and run:

```powershell
.\build\revival\Release\touchplus_usb_probe.exe
.\build\revival\Release\touchplus_probe.exe --list
.\build\revival\Release\touchplus_probe.exe
```

`touchplus_probe.exe --list` is safe and only enumerates video devices. Running the camera probe without arguments attempts one real frame capture.

## Planned phases

### Phase 0B — vendor control probe

Only after USB enumeration is stable enough to identify the device:

- read serial/flash data;
- read accelerometer values;
- exposure/gain control;
- LED/GPIO control;
- document the minimum USB control transfers needed to replace the old Etron dependency where possible.

### Phase 1 — stereo foundation

- modern OpenCV calibration/rectification;
- verify left/right orientation and baseline;
- persist calibration per physical Touch+ serial;
- produce a live rectified stereo viewer;
- triangulate known points and measure error.

### Phase 2 — modern hand tracking

Prototype modern 2D hand landmarks independently on each eye, then associate the same landmarks across both images and triangulate them into 3D.

Target output per hand:

```json
{
  "hand": "right",
  "confidence": 0.97,
  "landmarks": [
    {"id": 0, "x": 0.0, "y": 0.0, "z": 0.0}
  ]
}
```

The exact inference backend is intentionally not frozen yet. It should be chosen after the real sensor imagery from Phase 0/1 is available.

### Phase 3 — TouchPlus Runtime

One stable device/runtime API, with adapters for:

- Windows pointer/touch injection;
- gestures and hotkeys;
- OSC;
- MIDI;
- WebSocket/local API;
- Unity/Unreal or custom apps.

## Historical branches

- `master` — Jerry's historical July 2015 fork, preserved untouched.
- `archive/ractiv-2015-12-01` — last known Ractiv source state, imported as a read-only historical baseline.
- `revival/option-b-phase0` — active modern revival branch.

## Rule for the revival

Do not modernize the whole 2015 application tree. Build a narrow, testable modern stack next to it, and pull legacy code across only when it teaches us something hardware-specific.
