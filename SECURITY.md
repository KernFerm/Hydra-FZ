# Security

## Scope and safe use

Hydra FZ is for offline defensive experiments and authorized loopback service testing. Do not use it against systems or accounts without explicit permission.

Native mode performs no networking. The test secret is bounded to 32 bytes, held in RAM, omitted from session storage, and cleared on exit. Candidate lines are bounded to 64 bytes and streamed from microSD.

External mode permits only a fixed `LAB` operation. The Linux bridge reads configuration and candidates from fixed paths, validates a short allowlist of services, requires a loopback host, constructs an argument list without a shell, bounds UART input, applies a heartbeat, and promotes reports only after successful completion. UART is not an authentication boundary; physically secure the host and Flipper.

Reports can contain a successfully matched credential. Candidate files and reports should be protected and removed when no longer required.

## Failure behavior

Worker allocation is checked. SD read failures are distinct from end-of-file. Reports and sessions use temporary files and are synced before promotion. Malformed sessions and UART frames are rejected. Cancellation is checked during long delays and during companion startup. Arithmetic is bounded before conversion.

## Reporting vulnerabilities

Open a private GitHub security advisory for the repository. Include the affected version, reproduction steps, impact, and a proposed fix if available. Do not include real credentials or unauthorized target data.
