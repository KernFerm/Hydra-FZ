# External Hydra mode

External mode delegates network authentication to genuine THC Hydra on a Raspberry Pi or other Linux system. The Flipper is a UART controller/status display. The supplied configuration supports only an authorized service listening on the same Linux system.

## Wiring

Use 3.3 V UART only:

- Flipper TX (pin 13) → host adapter RX
- Flipper RX (pin 14) ← host adapter TX
- Flipper GND → host adapter GND

Do not connect a 5 V UART signal directly. A Raspberry Pi's GPIO UART is 3.3 V; enable it according to the Pi model/OS documentation and disable a serial console that occupies the selected port.

## Files

- `/var/lib/hydra-fz/config.json`: root-managed job configuration
- `/var/lib/hydra-fz/candidates.txt`: newline-delimited lab candidates
- `/var/lib/hydra-fz/output/hydra-report.txt`: completed genuine Hydra output

Allowed configuration keys are `host`, `service`, `port`, `login`, and `path`. Host must be `127.0.0.1`, `::1`, or `localhost`; service must be `ftp`, `ssh`, `http-get`, `https-get`, or `telnet`. The optional HTTP path is passed as Hydra's module option. Unknown keys and unsafe values are rejected.

Start the bridge with Python 3 and pyserial. Match the baud to the Flipper Settings value. The UART protocol is ASCII `HYD1`, uses bounded lines, supports `HELLO`, `STATUS`, `RUN LAB`, and `CANCEL`, and has a five-second heartbeat timeout.

The bridge never invokes a shell. A cancelled or failed run removes its partial file and preserves any previous complete report.
