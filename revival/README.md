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

## Confirmed physical-device smoke — 2026-08-19

The original physical Touch+ has now been exercised successfully on modern Windows.

Confirmed on real hardware:

- Windows enumerates the device as `USB\VID_1E4E&PID_0107`;
- the composite parent is healthy and the child camera interface appears as `Touch+ Camera` / `MI_00`;
- the recovered 32-bit Etron control stack initializes successfully;
- `eSPAEAWB_EnumDevice` sees the Touch+ alongside another webcam;
- `eSPAEAWB_SelectDevice` and `eSPAEAWB_SetSensorType(1)` succeed;
- `eSPAEAWB_SWUnlock(0x0107)` returns success;
- exposure/global-gain calls return successfully;
- the accelerometer returns live values;
- immediately after `SWUnlock`, Windows Camera displays a real live image from the Touch+.

The live-image test is reproducible. If Windows Camera switches away to another webcam and then returns to Touch+, the Touch+ may fall back to a uniform gray stream. Re-running the software unlock and then immediately opening Touch+ Camera restores real video. This strongly indicates that the useful sensor state is volatile across stream/session transitions.

**Consequence for the revival:** the production runtime must own the sequence atomically: vendor unlock/configuration first, then immediate video-open, without depending on another camera application.

This closes the major hardware viability question: the recovered Touch+ is not a dead device. Its camera path, vendor control path and IMU are all alive.

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

## Phase 0B — vendor control probe

`revival/tools/touchplus-unlock.ps1` uses the recovered Win32 Etron DLLs to validate the vendor-specific control plane independently from the video capture code.

Confirmed on the physical device:

- Etron initialization and device enumeration;
- Touch+ selection;
- OV7740/sensor type selection (`1`);
- `SWUnlock(0x0107)`;
- exposure and global-gain access;
- accelerometer access;
- real live video becomes available immediately after unlock.

The optional `-LegacyInit` path reproduces the recovered Ractiv initializer: disable AE/AWB, LEDs on, 15 ms exposure, global gain 1.0 and color gains 2/1/2.

## Phase 0C — atomic unlock + capture

Next canonical implementation step:

1. run the Etron unlock/control sequence;
2. keep control of the same Touch+ session;
3. immediately enumerate/open the video stream;
4. select the native `1280x480` mode;
5. capture and persist a frame;
6. split and verify `640x480` left/right images;
7. then keep the stream alive for a minimal stereo viewer.

This phase must not depend on Windows Camera or another application that can close/reopen the device and lose the volatile unlocked/configured state.

## Planned phases

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
