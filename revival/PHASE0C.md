# Phase 0C — Atomic unlock + stereo capture

Phase 0C removes Windows Camera from the critical path.

The physical-device smoke established that the Touch+ produces a real live image immediately after `eSPAEAWB_SWUnlock(0x0107)`, but the useful image state can be lost when another camera is selected or the stream is reopened. The recovered Ractiv code also performs vendor unlock immediately before opening the video stream.

`touchplus_atomic_probe.exe` therefore performs the complete sequence in one Win32 process:

1. initialize the recovered Etron control stack;
2. enumerate and select `Touch+ Camera`;
3. select sensor type `OV7740` (`1`);
4. send `eSPAEAWB_SWUnlock(0x0107)`;
5. sample the accelerometer as a control-plane health check;
6. release the temporary Etron handle;
7. immediately enumerate/open the Touch+ with Windows Media Foundation;
8. prefer the native `1280x480` MJPEG stereo mode at the highest exposed frame rate;
9. decode one frame to RGB32;
10. write the full image and `640x480` left/right halves as PNG;
11. compute simple luma statistics so a uniform gray frame is reported separately from a real image.

The Etron SDK shipped with the Touch+ is 32-bit, so this executable is intentionally built as **Win32/x86** even on 64-bit Windows.

## Artifact contents

The CI artifact `touchplus-phase0c-atomic-windows-x86` contains:

```text
touchplus_atomic_probe.exe
eSPAEAWBCtrl.dll
eSPDI.dll
EtLib.dll
```

Keep all four files together.

## Physical smoke

Close Windows Camera and any program that may be using either webcam. Then run:

```powershell
.\touchplus_atomic_probe.exe
```

Expected success ends with:

```text
Vendor unlock: PASS
Selected stereo mode: 1280x480 @ ... fps [MJPG]
Frame capture: PASS
Stereo split:  PASS (640x480 + 640x480)
Image-content check: PASS / non-uniform real image detected.
PHASE 0C RESULT: PASS
```

Output is written under:

```text
touchplus-atomic/
  touchplus-full.png
  touchplus-left.png
  touchplus-right.png
```

If capture succeeds but content is still nearly uniform, the probe returns a distinct result and the next controlled retry is:

```powershell
.\touchplus_atomic_probe.exe --legacy-init
```

`--legacy-init` additionally reproduces the recovered Ractiv initializer: disable AE/AWB, LEDs on, 15 ms exposure, global gain 1.0, and color gains 2/1/2.

Do not use `--legacy-init` first: the minimal unlock-only sequence is the canonical baseline because it already restored real video on the physical device.
