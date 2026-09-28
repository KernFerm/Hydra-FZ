#!/usr/bin/env python3
"""UART controller for genuine Hydra restricted to a loopback authentication lab."""
from __future__ import annotations
import argparse, json, os, pathlib, signal, subprocess, sys, threading
from dataclasses import dataclass
import serial

PROTOCOL = 1
ROOT = pathlib.Path("/var/lib/hydra-fz")
CONFIG = ROOT / "config.json"
CANDIDATES = ROOT / "candidates.txt"
OUTPUT = ROOT / "output"
REPORT = OUTPUT / "hydra-report.txt"
EXECUTABLES = ("/usr/bin/hydra", "/usr/local/bin/hydra")
SERVICES = {"ftp", "ssh", "http-get", "https-get", "telnet"}


@dataclass
class State:
    name: str = "IDLE"; attempts: int = 0; bytes: int = 0; found: int = 0
    failures: int = 0; invalid: int = 0; exit_code: int = 0; target: str = "none"; error: str = ""


def safe_token(value: str, limit: int = 63) -> str:
    clean = "".join(c if c.isalnum() or c in "._-" else "_" for c in value)
    return (clean or "none")[:limit]


def find_hydra() -> str:
    for candidate in EXECUTABLES:
        if pathlib.Path(candidate).is_file() and os.access(candidate, os.X_OK): return candidate
    raise FileNotFoundError("install_hydra_in_usr_bin_or_usr_local_bin")


def version_of(executable: str) -> str:
    result = subprocess.run([executable, "-h"], capture_output=True, text=True, timeout=5, check=False)
    first = (result.stdout or result.stderr).splitlines()
    return safe_token(first[0] if first else "unknown", 31)


def load_job() -> tuple[list[str], str, int]:
    data = json.loads(CONFIG.read_text(encoding="utf-8"))
    allowed = {"host", "service", "port", "login", "path"}
    if set(data) - allowed: raise ValueError("unknown_config_key")
    host = data.get("host", "127.0.0.1")
    service = data.get("service")
    login = data.get("login")
    port = data.get("port")
    path = data.get("path", "/")
    if host not in {"127.0.0.1", "::1", "localhost"}: raise ValueError("loopback_target_required")
    if service not in SERVICES: raise ValueError("unsupported_lab_service")
    if (not isinstance(login, str) or not 1 <= len(login) <= 64 or
            any(not (c.isalnum() or c in "._-@") for c in login)): raise ValueError("invalid_login")
    if not isinstance(port, int) or not 1 <= port <= 65535: raise ValueError("invalid_port")
    if not isinstance(path, str) or not path.startswith("/") or len(path) > 128 or any(ord(c) < 32 for c in path): raise ValueError("invalid_path")
    if not CANDIDATES.is_file(): raise FileNotFoundError("candidates.txt_missing")
    if CANDIDATES.stat().st_size > 16 * 1024 * 1024: raise ValueError("candidate_file_too_large")
    args = ["-l", login, "-P", str(CANDIDATES), "-s", str(port), "-t", "1", "-f", "-V"]
    if host == "::1": args += ["-6"]
    if service in {"http-get", "https-get"}: args += ["-m", path]
    args += [host, service]
    return args, f"{service}_{port}", CANDIDATES.stat().st_size


class Bridge:
    def __init__(self, port: str, baud: int, executable: str) -> None:
        self.executable = executable; self.version = version_of(executable); self.state = State()
        self.lock = threading.RLock(); self.serial_lock = threading.Lock(); self.process = None; self.thread = None
        self.cancel_requested = False; self.stop = False
        self.serial = serial.Serial(port, baud, timeout=0.25, write_timeout=1)

    def send(self, line: str) -> bool:
        try:
            with self.serial_lock:
                self.serial.write((line.rstrip("\r\n") + "\n").encode("ascii", "strict")); self.serial.flush()
            return True
        except (OSError, serial.SerialException):
            with self.lock: self.state.name = "ERROR"; self.state.error = "serial_io_failed"
            return False

    def send_info(self): self.send(f"HYD1 INFO {PROTOCOL} {self.version}")
    def send_status(self):
        with self.lock:
            s = self.state
            self.send(f"HYD1 STATUS {s.name} {s.attempts} {s.bytes} {s.found} {s.failures} {s.invalid} 0 {s.exit_code} {safe_token(s.target)}")

    def set_error(self, message: str):
        with self.lock: self.state.name = "ERROR"; self.state.error = safe_token(message); self.process = None
        self.send(f"HYD1 ERROR {safe_token(message)}")

    def run_job(self):
        temporary = REPORT.with_suffix(".txt.partial"); promoted = False; process = None
        try:
            OUTPUT.mkdir(parents=True, exist_ok=True, mode=0o700); os.chmod(OUTPUT, 0o700)
            arguments, target, size = load_job(); temporary.unlink(missing_ok=True)
            with self.lock:
                self.state = State(name="STARTING", bytes=size, target=target)
                if self.cancel_requested: self.state.name = "CANCELLED"; self.send_status(); return
            self.send_status()
            descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as report:
                process = subprocess.Popen([self.executable, *arguments], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", shell=False,
                    cwd=str(ROOT), start_new_session=True, bufsize=1)
                with self.lock:
                    self.process = process; pending = self.cancel_requested; self.state.name = "STOPPING" if pending else "RUNNING"
                self.send_status()
                if pending:
                    try: os.killpg(process.pid, signal.SIGTERM)
                    except OSError: pass
                assert process.stdout is not None
                for line in process.stdout:
                    report.write(line)
                    with self.lock:
                        if "[ATTEMPT]" in line: self.state.attempts += 1
                        if "login:" in line and "password:" in line and "[ATTEMPT]" not in line: self.state.found += 1
                    if self.cancel_requested:
                        try: os.killpg(process.pid, signal.SIGTERM)
                        except OSError: pass
                    self.send_status()
                return_code = process.wait(); report.flush(); os.fsync(report.fileno())
            with self.lock:
                cancelled = self.cancel_requested or self.state.name == "STOPPING"; self.process = None; self.state.exit_code = return_code
                self.state.failures = max(0, self.state.attempts - self.state.found)
            if not cancelled and return_code == 0:
                os.replace(temporary, REPORT); promoted = True
                fd = os.open(REPORT.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
                try: os.fsync(fd)
                finally: os.close(fd)
            with self.lock: self.state.name = "CANCELLED" if cancelled else ("COMPLETE" if promoted else "FAILED")
            temporary.unlink(missing_ok=True); self.send_status()
        except (OSError, ValueError, UnicodeError, json.JSONDecodeError, subprocess.SubprocessError) as error:
            if process is not None and process.poll() is None:
                try: os.killpg(process.pid, signal.SIGTERM); process.wait(timeout=5)
                except (OSError, subprocess.TimeoutExpired):
                    try: os.killpg(process.pid, signal.SIGKILL); process.wait(timeout=5)
                    except (OSError, subprocess.TimeoutExpired): pass
            if not promoted:
                try: temporary.unlink(missing_ok=True)
                except OSError: pass
            self.set_error(str(error))

    def start(self):
        with self.lock:
            if self.process is not None or (self.thread and self.thread.is_alive()): self.send("HYD1 ERROR busy"); return
            self.cancel_requested = False; self.thread = threading.Thread(target=self.run_job, daemon=True); self.thread.start()

    def cancel(self):
        with self.lock:
            process = self.process; running = self.thread is not None and self.thread.is_alive()
            if process is None and not running: self.send("HYD1 ERROR not_running"); return
            self.cancel_requested = True; self.state.name = "STOPPING"
        self.send_status()
        if process is None: return
        try: os.killpg(process.pid, signal.SIGTERM); process.wait(timeout=5)
        except (OSError, subprocess.TimeoutExpired):
            try: os.killpg(process.pid, signal.SIGKILL); process.wait(timeout=5)
            except (OSError, subprocess.TimeoutExpired): self.set_error("process_would_not_stop")

    def handle(self, line: str):
        parts = line.strip().split()
        if parts == ["HYD1", "HELLO"]: self.send_info(); self.send_status()
        elif parts == ["HYD1", "STATUS"]: self.send_status()
        elif parts == ["HYD1", "RUN", "LAB"]: self.start()
        elif parts == ["HYD1", "CANCEL"]: self.cancel()
        else: self.send("HYD1 ERROR invalid_command")

    def serve(self):
        self.send_info()
        while not self.stop:
            raw = self.serial.readline(257)
            if len(raw) > 256: self.send("HYD1 ERROR line_too_long"); self.serial.reset_input_buffer(); continue
            if raw:
                try: self.handle(raw.decode("ascii", "strict"))
                except UnicodeDecodeError: self.send("HYD1 ERROR non_ascii_command")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--port", required=True); parser.add_argument("--baud", type=int, choices=(115200,230400,460800), default=115200); args = parser.parse_args()
    bridge = None
    try: bridge = Bridge(args.port, args.baud, find_hydra()); bridge.serve()
    except KeyboardInterrupt:
        pass
    except (OSError, ValueError, UnicodeError, subprocess.SubprocessError, serial.SerialException) as error:
        print(f"Hydra FZ bridge error: {safe_token(str(error), 160)}", file=sys.stderr); return 1
    finally:
        if bridge:
            if bridge.process or (bridge.thread and bridge.thread.is_alive()): bridge.cancel()
            try: bridge.serial.close()
            except (OSError, serial.SerialException): pass
    return 0

if __name__ == "__main__": raise SystemExit(main())
