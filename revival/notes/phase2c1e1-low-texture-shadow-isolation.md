# Phase 2C.1E.1 — low-texture shadow telemetry isolation

Purpose: make the existing Phase 2C.1E counterfactual NCC telemetry attributable specifically to authoritative `TextureLow` rejects.

Physical 2C.1E smoke showed that the existing `[FWD] best_ncc_max / second_ncc_at_best / best_minus_second / winning_disparity` fields were still aggregated across all forward failures in the frame. When `TextureLow`, `CorrelationLow` and `UniquenessFail` co-existed, a high NCC value could therefore come from a non-texture failure and could not justify relaxing the texture floor.

2C.1E.1 keeps all authoritative behavior unchanged. The diagnostic helper still classifies the authoritative failure exactly as before, but it now exports NCC/gap/disparity values only for authoritative `TextureLow` probes. The tracker already aggregates those exported fields, so the existing `[FWD]` line becomes texture-shadow-specific without touching the matcher path.

Safety boundary remains unchanged:

- no change to `search_left_to_right()` or `mutually_consistent_match()`;
- no change to patch size, disparity limits, `kMinTextureVariance`, NCC threshold or uniqueness gap;
- no change to calibration/K-D-R-T-P-Q, surface frame, identity/fusion, A/B, smoothing or contact semantics;
- no OS input injection.

Physical gate: repeat the same hover -> near -> sustained-contact smoke and inspect `[METRIC]` + `[FWD]`. For frames where `texture_low > 0`, the reported `best_ncc_max`, `second_ncc_at_best`, `best_minus_second` and `winning_disparity` now describe only low-texture counterfactual probes.

PR remains DRAFT / DO NOT MERGE until the real-device result is reviewed.
