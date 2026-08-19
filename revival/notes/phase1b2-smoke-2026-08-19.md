# Phase 1B.2a physical smoke — 2026-08-19

Physical unit: `0101007379`.

Observed with the first guided PowerShell helper:

- `-Pairs 3` was rejected by an unnecessary minimum-8 guard;
- with 8 selected, the workflow presented console prompts but no live stereo preview;
- the resulting one-shot capture was gray/unusable.

This is treated as a capture-workflow failure, not a calibration-target failure. The printed target independently passed physical scale verification: the 100 mm reference measured exactly 100 mm.

Corrective action:

- retire the repeated one-shot `touchplus_atomic_probe.exe` calibration loop;
- use one persistent `touchplus_calibration_capture.exe` session;
- Etron unlock once, camera stream open continuously;
- LEFT/RIGHT live preview always visible;
- SPACE saves the next synchronized stereo frame;
- values below 8 are allowed with `--pairs N` for smoke testing;
- nearly uniform/gray frames are rejected before saving;
- saved frames already use the historical Ractiv vertical orientation correction.

Acceptance test: run `touchplus_calibration_capture.exe --pairs 3`, confirm healthy live LEFT/RIGHT video, save three visibly non-gray poses, then inspect the six eye PNGs before collecting the full 18–25 pose set.
