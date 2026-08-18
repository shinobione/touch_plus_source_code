# Phase 1B — Factory calibration recovery status

## Goal

Recover the original per-device Ractiv calibration inputs for the physical Touch+ before falling back to a new local stereo calibration.

## Historical behavior recovered from the 2015 Ractiv source

On the first live frame, the tracking core:

1. calls `Camera::getSerialNumber()`;
2. reads 10 bytes through `eSPAEAWB_ReadFlash(serialNumber, 10)`;
3. concatenates each byte as a decimal number into the Ractiv serial string;
4. validates that the resulting string has 10 characters and starts with `0101`;
5. uses `userdata/<serial>` as the device-specific calibration directory.

`Reprojector::load()` expects the following factory-derived files:

- `0.jpg`
- `1.jpg`
- `stereoCalibData.txt`
- `rect0.txt`
- `rect1.txt`

If the first three were absent, the historical runtime attempted to fetch them from:

`http://d2i9bzz66ghms6.cloudfront.net/data/<derived-key>/...`

The derived key is made from characters 4..9 of the 10-character Ractiv serial with leading zeroes removed. The two rectification files are then generated locally from the calibration images.

## Revival probe

`revival/tools/touchplus-factory-calibration-probe.ps1`:

- relaunches itself under 32-bit Windows PowerShell because the recovered Etron SDK is Win32;
- enumerates the physical Touch+;
- selects OV7740;
- performs `SWUnlock(0x0107)`;
- reads the 10-byte flash serial;
- prints raw decimal and hexadecimal bytes plus the exact Ractiv-style serial;
- derives the historical cloud key;
- makes no network request unless `-TryFactoryDownload` is explicitly supplied;
- only downloads JPG/TXT calibration inputs, never executable content.

## Physical finding — 2026-08-19

On the real Touch+, `eSPAEAWB_ReadFlash(buffer, 10)` returned **`10`** after successful device enumeration, selection, OV7740 sensor selection and software unlock.

The first version of the revival PowerShell probe incorrectly treated every non-zero return value as a failure. That interpretation was not justified for `ReadFlash`: the historical Ractiv code ignores this return value and consumes the 10-byte buffer directly, while the Etron header documents negative values for failures. A return of `10` on a 10-byte request is therefore treated as a successful/usable transfer indication unless the payload validation proves otherwise.

The probe now:

- treats negative `ReadFlash` values as errors;
- accepts `0`, `10`, and other non-negative values provisionally;
- validates the actual 10-byte payload;
- rejects an all-zero payload;
- prints both decimal bytes and hexadecimal bytes before deriving any serial/key.

## Current acceptance gate

Run the corrected probe on the physical Touch+ and capture:

- `Raw flash bytes`
- `Raw flash hex`
- `Ractiv serial string`
- historical serial check result
- historical cloud key

If the serial is plausible, the next step is the explicit `-TryFactoryDownload` probe. If the old CDN data is unavailable, Phase 1B falls back to a modern local stereo calibration workflow.
