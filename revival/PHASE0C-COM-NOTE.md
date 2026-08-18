# Phase 0C COM apartment compatibility

Physical smoke of the first atomic Win32 build reached vendor unlock successfully and then failed at Media Foundation startup with:

```text
PHASE 0C RESULT: FAIL
CoInitializeEx failed (HRESULT 0x80010106)
```

`0x80010106` is `RPC_E_CHANGED_MODE`: the recovered Etron control stack had already initialized COM on the calling thread using a different apartment model before the probe attempted `CoInitializeEx(..., COINIT_MULTITHREADED)`.

Phase 0C now force-includes `src/com_compat.h` for the Win32 atomic target. The shim:

- calls the real `CoInitializeEx`;
- treats only `RPC_E_CHANGED_MODE` as an already-initialized thread rather than a fatal error;
- reuses the apartment already established by Etron;
- calls `CoUninitialize` only when the probe itself acquired a COM initialization count.

This keeps the recovered vendor stack and Media Foundation in the same process without unbalancing COM initialization ownership.
