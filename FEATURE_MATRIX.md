# Feature matrix

| Upstream Hydra feature | Native Flipper | External loopback lab |
|---|---:|---:|
| Candidate iteration | Yes: PIN generator or text file | Genuine Hydra `-P` file |
| Username/password pair scheduling | Single RAM-only local secret | Genuine Hydra with one fixed configured login |
| Stop after a valid result | Yes | Genuine Hydra `-f` |
| Attempt limit | Yes | Candidate file and single-task bounds |
| Cancellation | Yes | Yes, terminates the child process |
| Resume | Native candidate index and counters | New fixed run; external partial output is discarded |
| Real measured progress | Yes | Yes, parsed from genuine Hydra output |
| SSH/FTP/HTTP/Telnet modules | No | Yes, against loopback only |
| Arbitrary remote targets | No | No |
| Rate-limit model | Yes | Determined by the local service |
| Lockout model | Yes | Determined by the local service |
| Increasing-delay model | Yes | Determined by the local service |
| Transactional report | Yes | Yes |
| NFC/RFID access guessing | No | No |

“No” entries are deliberate boundaries, not placeholders.
