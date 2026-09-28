# Changelog

## 1.0.3 — 2026-09-27

- Corrected the CodeQL high-severity narrow/wide loop comparison in session attempt-limit validation.
- Changed the array loop index to `size_t` and narrowed it only after the fixed array bound proves the conversion safe.
- Made intentional 8-bit GUI item-count conversions explicit.
- Rebuilt, regression-tested, and rescanned the target f7/API 87.1 application.

## 1.0.2 — 2026-09-27

- Corrected native resume handling, session validation, and attempt-limit completion behavior.
- Hardened UART startup/frame handling and Linux companion process/report cleanup.
- Refreshed version metadata and rebuilt for target f7/API 87.1.

## 1.0.0 — 2026-09-27

- Added real offline numeric-PIN and microSD candidate processing.
- Added RAM-only test secrets, exact comparison, attempt bounds, cancellation, and resume.
- Added measured rate-limit, lockout, and increasing-delay defense models.
- Added transactional sessions and measured reports.
- Added a bounded UART controller and a loopback-only Linux/Raspberry Pi companion for genuine THC Hydra.
- Added settings, About/provenance, documentation, host tests, and official firmware build configuration.
