# Testing

Run host checks from PowerShell:

```powershell
.\tests\run_tests.ps1
```

The suite checks numeric PIN ordering and bounds, exact comparisons, each protection delay model, invalid configuration, loopback-only companion configuration, fixed argument construction, input injection rejection, candidate-file size bounds, the restricted UART command, and Python syntax.

Build against official firmware 1.4.3+ with:

```sh
./fbt fap_hydra_fz
```

## Hardware acceptance checklist

- Set a PIN secret and confirm the matching candidate and exact attempt count.
- Run a candidate file with empty, CRLF, 64-byte, and overlong lines.
- Confirm each protection model changes measured throughput as configured.
- Cancel during generation and during a protection delay; confirm the UI remains responsive.
- Reload a session, re-enter the secret, and confirm the next candidate is not repeated.
- Remove or fill the SD card during reading/writing; confirm failure is reported and no partial report is promoted.
- Repeat at least 25 runs and verify resources are released.
- Connect a 3.3 V UART Linux host, verify heartbeat timeout/disconnect, malformed frames, startup cancellation, successful loopback lab operation, and preservation of an older report after failure.

Hardware items require the user's actual Flipper, microSD, adapter, and authorized local test service. They are not claimed complete from host-only tests.

## Verified build — 2026-09-27

- Version: 1.0.3
- Python companion tests: 5 passed
- Python syntax compilation: passed
- Authenticated Snyk Code scan at low-or-higher severity: 0 issues
- uFBT application check: target 7, API 87.1, passed with no unresolved symbols
- Artifact: `dist/hydra_fz.fap`, 29,380 bytes
- SHA-256: `4FC2D68BAFDE75A04DD7579708E11F5336754F4FED6E2002D542ECEF28D12F23`

The artifact hash above is updated after the final protocol hardening rebuild.
