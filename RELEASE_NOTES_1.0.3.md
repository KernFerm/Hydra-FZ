# Hydra FZ v1.0.3

Hydra FZ brings reusable THC Hydra candidate/session concepts to Flipper Zero without claiming that the stock device contains Linux networking or Hydra's SSH, FTP, HTTP, or Telnet modules.

## Security correction in v1.0.3

GitHub CodeQL identified a high-severity `cpp/comparison-with-wider-type` alert in restored attempt-limit validation. An 8-bit loop counter was compared with the wider result of `COUNT_OF`, which CodeQL correctly treats as a possible overflow/infinite-loop pattern if the wider bound can exceed the counter's range.

Version 1.0.3 corrects the root cause:

- The array loop counter is now `size_t`, matching the width used by `COUNT_OF`.
- Conversion to the GUI's 8-bit index occurs only after the fixed four-entry array bound proves it safe.
- GUI APIs that intentionally accept 8-bit value counts now receive explicit conversions.
- No CodeQL alert was dismissed or suppressed.
- The corrected source was clean-built, regression-tested, and rescanned.

## Native offline authentication lab

Native mode operates entirely on the Flipper and compares real candidates against a local test secret held only in RAM.

- Generate numeric PIN candidates from 1 to 8 digits.
- Stream newline-delimited candidates directly from a microSD file.
- Perform exact comparisons and stop when the local test secret is found.
- Configure an attempt limit.
- Select no protection, rate limiting, account lockout, or increasing delay.
- Display measured attempts, candidates read, rejected lines, bytes considered, attempts per second, active delay, and lockout events.
- Cancel safely without blocking the interface.
- Save and restore candidate position, counters, source, and protection configuration.
- Keep the local secret out of session storage; it must be entered again after loading a session.
- Save measured reports transactionally so a failed write cannot replace the previous completed report with partial data.

Native mode performs no networking and does not simulate successful authentication to SSH, FTP, HTTP, SMB, RDP, databases, or remote hosts.

## Genuine THC Hydra on Raspberry Pi/Linux

External mode uses the Flipper as a 3.3 V UART controller and live status display while genuine THC Hydra runs on a Raspberry Pi, Linux laptop, desktop, mini PC, or VM.

The supplied companion accepts only the fixed `LAB` operation and restricts targets to `127.0.0.1`, `::1`, or `localhost`. Authorized loopback SSH, FTP, HTTP GET, HTTPS GET, and Telnet lab services are supported. UART cannot provide an arbitrary remote target, executable path, filename, Hydra option, or shell command.

The companion invokes Hydra using a fixed argument array with `shell=False`. The Flipper displays genuine process state, attempts, found results, failures, candidate-file bytes, exit status, disconnects, and protocol errors. Cancellation terminates the child process; failed or cancelled output is not promoted as a completed report.

## Install

Requirements:

- Flipper Zero with official firmware 1.4.3 or later;
- a microSD card installed in the Flipper.

1. Download the attached `hydra_fz.fap`.
2. Connect the Flipper by USB and open qFlipper.
3. Copy the FAP to `/ext/apps/Tools/`.
4. Open **Apps → Tools → Hydra FZ**.

## Native quick start

1. Select **Set local test secret**. It remains in RAM only.
2. In **Settings**, select PIN or file candidates, PIN length, protection model, and attempt limit.
3. For file mode, select **Choose candidate file** and choose a newline-delimited microSD file.
4. Select **Run / resume lab**. Back requests safe cancellation.
5. Use **Load saved session**, re-enter the same secret, and run again to resume.
6. Select **View last report** for measured results.

For Linux installation, fixed paths, UART wiring, and configuration, see `EXTERNAL_HYDRA.md` in the repository. A Raspberry Pi is optional; another Linux computer or VM with appropriate UART access can be used.

## Safety and licensing

Use Hydra only on systems you own or are explicitly authorized to test. Hydra FZ performs no NFC, RFID, Sub-GHz, iButton, badge, door-lock, or physical-access credential guessing.

Hydra FZ is licensed under GNU AGPL v3 and records its pinned upstream revision and provenance in `UPSTREAM_VERSION.md`.

## Build verification

- Host companion regression tests: 5 passed.
- uFBT APPCHK: target f7/API 87.1 with no unresolved symbols.
- Authenticated Snyk Code scan: 0 issues at low-or-higher severity.
- FAP size: 29,380 bytes.
- SHA-256: `4FC2D68BAFDE75A04DD7579708E11F5336754F4FED6E2002D542ECEF28D12F23`
