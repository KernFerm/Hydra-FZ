# Porting analysis

## Upstream behavior reviewed

The pinned THC Hydra source was inspected for its login/password iteration, pair advancement, `-f` stop-on-success behavior, task accounting, restore file, output, target/service selection, concurrency, and option parsing. Hydra's network modules depend on sockets, TLS/SSH and service-specific libraries that official Flipper firmware does not provide.

## Native mapping

Hydra FZ preserves the useful bounded core of that workflow: a real candidate source, deterministic iteration, exact local comparison, stop on success, an attempt ceiling, cancellation, measured statistics, and resumable indices. Numeric PIN order is `0000`, `0001`, and so on for four digits. File order is exactly line order. Input streams from microSD; it is never accumulated as a complete list.

The native target is intentionally a RAM-only test secret. Rate limiting, lockout, and increasing delay are measured local defense models. They demonstrate how controls change candidate throughput without claiming to authenticate to a network service.

## External mapping

The companion invokes the pinned system `hydra` executable using a fixed argument array and no shell. Genuine Hydra owns protocol behavior and result parsing. The bridge accepts only the `LAB` command and only loopback hosts from a fixed root-owned configuration file. UART users cannot provide a target, option, filename, or shell fragment.

## Excluded behavior

Native SSH, FTP, HTTP, SMB, database, and other service modules are not feasible or represented as present. Distributed operation, arbitrary targets, proxying, username enumeration, password mutation, and parallel attack tuning are not exposed. NFC, LF RFID, Sub-GHz, and iButton are unrelated to Hydra and are not used.
