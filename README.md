# Hydra FZ

Hydra FZ is a Flipper Zero authentication-security lab built around genuine concepts from [THC Hydra](https://github.com/vanhauser-thc/thc-hydra). It has two deliberately separate modes:

Current release: **v1.0.2**.

- **Native Flipper mode** performs real, offline candidate comparisons against a test secret held only in RAM. It supports generated numeric PINs or a newline-delimited candidate file, attempt limits, cancellation/resume, reports, and measured simulations of rate limiting, account lockout, and increasing delay.
- **External Hydra mode** controls a genuine THC Hydra process running on a Raspberry Pi or another Linux computer over 3.3 V UART. The supplied bridge is intentionally limited to services on that Linux computer's loopback interface; it cannot be redirected to a remote target through the Flipper.

The stock Flipper Zero does not contain the networking stack or protocol libraries needed to run Hydra's SSH, FTP, HTTP, or other network modules itself. Native mode therefore does not pretend to perform network logins.

## Install the FAP

Hydra FZ requires official Flipper firmware 1.4.3 or later.

1. Download `hydra_fz.fap` from the GitHub release.
2. Connect the Flipper Zero by USB and open qFlipper.
3. Open the microSD card and copy the FAP to `/ext/apps/Tools/`.
4. Safely disconnect, then open **Apps → Tools → Hydra FZ**.

For development, place this directory under `applications_user/hydra_fz` in a matching firmware tree and run:

```sh
./fbt fap_hydra_fz
```

## Native offline lab

1. Select **Set local test secret**. The value stays in RAM and is erased when the app exits.
2. In **Settings**, select either generated numeric PINs or a candidate file, PIN length, a protection model, and an attempt limit.
3. For file mode, use **Choose candidate file**. Each non-empty line is one candidate; overlong lines are rejected.
4. Select **Run / resume lab**. The screen shows actual comparisons, measured throughput, applied delays, rejected lines, and lockouts.
5. Press Back to cancel safely. The app writes a session without the secret. Set the same secret again and load the session to resume.
6. Open **View last report** for measured results. Reports and sessions are written transactionally under the app data directory.

The protection choices are local defensive models, not claims about any external service. No ETA is fabricated.

## Raspberry Pi or Linux external mode

A Raspberry Pi is optional. A Linux laptop, desktop, or VM works too. Install genuine Hydra, Python 3, and pyserial:

```sh
sudo apt update
sudo apt install hydra python3 python3-pip
python3 -m pip install -r companion/requirements.txt
```

Copy `companion/config.example.json` to `/var/lib/hydra-fz/config.json`, place the test candidates at `/var/lib/hydra-fz/candidates.txt`, and configure a service you own on `127.0.0.1`, `::1`, or `localhost`. Connect crossed UART TX/RX plus GND at **3.3 V only**, select the same baud on both sides, then run:

```sh
python3 companion/hydra_fz_bridge.py --port /dev/serial0 --baud 115200
```

Open **External Hydra lab** on the Flipper. OK starts or cancels the fixed local-lab job. See [EXTERNAL_HYDRA.md](EXTERNAL_HYDRA.md) for wiring, configuration, and protocol details.

Use Hydra only on systems you own or have explicit permission to test. The bridge's loopback restriction is intentional and must not be represented as full remote Hydra support.

## Compatibility and provenance

- Flipper firmware: official 1.4.3+
- App version: 1.0.2
- Upstream snapshot: THC Hydra v9.8dev, commit `17b52613320257a620ce3b5611b2e8196eb6e7fc`
- License: GNU Affero General Public License v3; see [LICENSE](LICENSE)

The native lab reuses bounded candidate iteration, stop-on-success, and restore concepts identified in upstream Hydra. Network protocol implementations remain in the genuine external Hydra executable.

## Privacy and storage

The native secret is never written to the session file. A successful report contains the matching candidate, and external Hydra output can contain credentials, so protect or delete reports when finished. Large wordlists are streamed from microSD and are not committed to this repository.

## Status

Automated host tests and the target firmware build are documented in [TESTING.md](TESTING.md). Hardware/UART behavior must still be confirmed with the exact Flipper, SD card, wiring, Linux host, and authorized loopback service used by the end user.
