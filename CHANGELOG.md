# Changelog

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
