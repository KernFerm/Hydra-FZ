# Hydra FZ v1.0.2

Hydra FZ provides a real native offline authentication-defense lab with PIN/file candidates, measured protection models, cancellation, sessions, and transactional reports. Its optional external mode controls genuine THC Hydra against an authorized loopback lab service on a Raspberry Pi or other Linux computer.

Version 1.0.2 corrects resume handling, strengthens session validation, prevents unusable attempt-limit sessions, hardens UART startup and malformed-frame handling, and improves Linux companion process and report cleanup. It is clean-built and validated for Flipper target f7/API 87.1.

Download `hydra_fz.fap` and copy it to `/ext/apps/Tools/` on the Flipper.

SHA-256: `C30CA414CC2DD984ADFB52D1EAC55C8FAB328D17F155451874CC8121CEC44976`
