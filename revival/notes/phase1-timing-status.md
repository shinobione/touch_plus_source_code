# Phase 1 timing diagnostics

Physical Touch+ results as of 2026-08-19:

- Media Foundation native mode advertises 1280x480 MJPEG @ 60 fps.
- Raw Media Foundation delivery measures about 29.9 samples/s with ~32 ms timestamps.
- Historical DirectShow IAMStreamConfig negotiation also accepts 1280x480 MJPEG @ 60 fps but delivers about 30.6 callbacks/s with ~32 ms SampleTime.
- Recovered Ractiv sensor initialization was manually validated on hardware: DisableAE OK, DisableAWB OK, LEDs ON OK, exposure both = 15 ms OK, global gain left/right = 1 OK, color gains left/right = 2/1/2 OK.
- An atomic DirectShow + legacy sensor-init timing probe is now packaged as `touchplus_directshow_legacy_timing_probe.exe` so the sensor initialization and stream negotiation happen in one process before cadence measurement.

Next physical acceptance test: run the atomic legacy-init probe and compare its measured callback/sample cadence with the ~30 fps minimal-unlock baseline.
