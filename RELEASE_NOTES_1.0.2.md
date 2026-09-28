# Hydra FZ v1.0.2

Hydra FZ brings reusable THC Hydra candidate and session concepts to Flipper Zero without claiming that the stock device contains Linux networking or Hydra's network protocol modules.

## What the application does

Hydra FZ has two separate, real operating modes.

### Native offline authentication lab

Native mode operates entirely on the Flipper and compares real candidates against a local test secret held only in RAM.

- Generate numeric PIN candidates from 1 to 8 digits.
- Stream newline-delimited candidates directly from a microSD file.
- Perform exact comparisons and stop when the local test secret is found.
- Configure an attempt limit and select no protection, rate limiting, account lockout, or increasing delay.
- Display measured attempts, candidates read, invalid or overlong lines, bytes considered, attempts per second, active delay, and lockout events.
- Cancel without blocking the user interface.
- Save and restore candidate position, counters, source, and protection settings.
- Keep the secret out of session storage; it must be entered again after restoring a session.
- Save measured reports transactionally so an incomplete write cannot silently replace a completed report.

Native mode performs no networking. It does not simulate SSH, FTP, HTTP, SMB, RDP, database, or remote-host authentication.

### Genuine THC Hydra on Linux

External mode uses the Flipper as a 3.3 V UART controller and status display while genuine THC Hydra runs on a Raspberry Pi, Linux laptop, desktop, mini PC, or Linux VM.

The supplied companion accepts only the fixed `LAB` operation and restricts the Hydra target to `127.0.0.1`, `::1`, or `localhost`. Authorized loopback SSH, FTP, HTTP GET, HTTPS GET, and Telnet lab services are supported. The UART interface cannot supply an arbitrary remote target or shell command.

The Flipper displays genuine process state, attempts, found results, failures, candidate-file bytes, exit status, disconnects, and protocol errors. Cancellation terminates the Linux child process and incomplete output is discarded.

## Changes in v1.0.2

- Fixed restored progress being cleared when the required RAM-only secret was re-entered.
- Prevented attempt-limit completion from creating a session that could not advance.
- Synchronized restored source, PIN length, protection mode, and attempt limit with Settings.
- Strengthened session counter and configuration validation.
- Made cancellation effective while advancing a file to its saved position.
- Preserved previous valid reports and sessions when final file promotion fails.
- Released UART resources when the initial companion handshake fails.
- Rejected non-printable, overlong, malformed, and trailing-data UART frames.
- Added visible heartbeat timeout and disconnection errors.
- Ensured unexpected companion failures terminate genuine Hydra.
- Restricted external output directories and reports to owner-only permissions.
- Added correct Hydra IPv6 handling for an authorized `::1` loopback lab.

## Install

Hydra FZ requires official Flipper firmware 1.4.3 or later and a microSD card.

1. Download the attached `hydra_fz.fap`.
2. Connect the Flipper by USB and open qFlipper.
3. Copy the FAP to `/ext/apps/Tools/`.
4. Open **Apps → Tools → Hydra FZ**.

## Native quick start

1. Select **Set local test secret**. The value remains in RAM only.
2. In **Settings**, select PIN or file candidates, PIN length, protection model, and attempt limit.
3. For file mode, select **Choose candidate file** and choose a newline-delimited microSD file.
4. Select **Run / resume lab**. Back requests safe cancellation.
5. Use **Load saved session**, re-enter the same secret, and run again to resume.
6. Select **View last report** for measured results.

For external setup, wiring, fixed paths, and Linux commands, read `EXTERNAL_HYDRA.md` in the repository.

## Verification and license

- Companion regression tests: 5 passed.
- Authenticated Snyk Code scan: 0 issues at low-or-higher severity.
- uFBT APPCHK: target f7/API 87.1, no unresolved symbols.
- FAP size: 29,372 bytes.
- SHA-256: `C30CA414CC2DD984ADFB52D1EAC55C8FAB328D17F155451874CC8121CEC44976`

Use Hydra only on systems you own or are explicitly authorized to test. Hydra FZ performs no NFC, RFID, Sub-GHz, iButton, badge, door-lock, or physical-access credential guessing. Licensed under GNU AGPL v3 with upstream provenance recorded in `UPSTREAM_VERSION.md`.
