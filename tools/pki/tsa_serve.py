# -*- coding: utf-8 -*-
"""tsa_serve.py — локальная служба времени RFC 3161 для тестов (план 4.1, эталон 7.4: сети в тестах нет).

Запуск: "$env:OTTISK_PYTHON" -X utf8 -I tools/pki/tsa_serve.py [--pki <tests/fixtures/pki>] [--port 0]
        [--openssl <openssl.exe>] [--lifetime 900] [--no-stdin] [--verbose]

Первая строка stdout — номер порта на 127.0.0.1 (по умолчанию свободный). Сервер работает, пока открыт stdin
(тест закрыл канал или умер — выход), не дольше --lifetime секунд.

Пути:
  POST /tsr          application/timestamp-query → application/timestamp-reply: `openssl ts -reply -config
                     tools/pki/tsa.cnf` (подписант — tsa.pem тестовой PKI, цепочка — chain.pem, политика 1.2.3.4.1);
                     другой Content-Type — 415
  POST /reject       ответ со статусом rejection (PKIStatus 2) без токена
  POST /garbage      200 и application/timestamp-reply, но тело — не DER
  POST /wrongtype    200, text/plain
  POST /http500      500
  POST /slow?ms=N    обычный ответ через N мс (по умолчанию 3000) — таймаут клиента
  GET  /redirect?to=URL   302 на URL — проверка «без редиректов на другой хост»
  GET  /             200, «AcroBatya test TSA»
Код выхода 77 — нет openssl.exe или фикстур PKI (тест даёт SKIP), 2 — неверные аргументы.
openssl — 3.5 из Git for Windows (mingw64) или --openssl / OTTISK_OPENSSL.
"""
from __future__ import annotations

import argparse
import http.server
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG = Path(__file__).resolve().parent / "tsa.cnf"
QUERY_TYPE = "application/timestamp-query"
REPLY_TYPE = "application/timestamp-reply"
MAX_QUERY = 64 * 1024
# TimeStampResp { status PKIStatusInfo { status 2 (rejection) } } без токена.
REJECTION = bytes.fromhex("30053003020102")


def find_openssl(hint: str | None) -> str | None:
    for c in (hint, os.environ.get("OTTISK_OPENSSL"), r"C:\Program Files\Git\mingw64\bin\openssl.exe",
              r"C:\Program Files\Git\usr\bin\openssl.exe"):
        if c and Path(c).is_file():
            return c
    return shutil.which("openssl")


class Tsa:
    def __init__(self, openssl: str, pki: Path, work: Path, verbose: bool) -> None:
        self.openssl = openssl
        self.pki = pki
        self.work = work
        self.verbose = verbose
        self.lock = threading.Lock()
        self.count = 0
        serial = work / "tsaserial"
        serial.write_text("01\n", encoding="ascii")
        self.env = dict(os.environ, OTTISK_TSA_PKI=str(pki), OTTISK_TSA_SERIAL=str(serial), MSYS_NO_PATHCONV="1")

    def reply(self, query: bytes) -> bytes:
        # Номер серии в tsaserial общий — ответы по одному.
        with self.lock:
            self.count += 1
            q = self.work / f"q{self.count}.tsq"
            r = self.work / f"r{self.count}.tsr"
            q.write_bytes(query)
            p = subprocess.run([self.openssl, "ts", "-reply", "-config", str(CONFIG), "-section", "tsa_test",
                                "-queryfile", str(q), "-out", str(r)], env=self.env, capture_output=True,
                               timeout=60, check=False)
            if p.returncode != 0 or not r.is_file():
                raise RuntimeError(p.stderr.decode("utf-8", "replace").strip() or f"openssl ts: {p.returncode}")
            data = r.read_bytes()
            q.unlink(missing_ok=True)
            r.unlink(missing_ok=True)
            return data


def make_handler(tsa: Tsa):
    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"
        server_version = "AcroBatyaTestTSA/1.0"

        def log_message(self, fmt: str, *args) -> None:  # noqa: A003 - имя из BaseHTTPRequestHandler
            if tsa.verbose:
                sys.stderr.write("tsa: " + (fmt % args) + "\n")

        def send(self, code: int, body: bytes, ctype: str = "text/plain; charset=utf-8",
                 extra: dict[str, str] | None = None) -> None:
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            for k, v in (extra or {}).items():
                self.send_header(k, v)
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:  # noqa: N802 - имя из BaseHTTPRequestHandler
            url = urllib.parse.urlsplit(self.path)
            if url.path == "/redirect":
                to = urllib.parse.parse_qs(url.query).get("to", ["http://example.invalid/"])[0]
                self.send(302, b"", extra={"Location": to})
            elif url.path == "/":
                self.send(200, b"AcroBatya test TSA\n")
            else:
                self.send(404, b"not found\n")

        def do_POST(self) -> None:  # noqa: N802
            url = urllib.parse.urlsplit(self.path)
            length = int(self.headers.get("Content-Length") or 0)
            if length <= 0 or length > MAX_QUERY:
                self.send(400, b"bad length\n")
                return
            body = self.rfile.read(length)
            ctype = (self.headers.get("Content-Type") or "").split(";")[0].strip().lower()
            if url.path == "/http500":
                self.send(500, b"server error\n")
                return
            if url.path == "/redirect":
                to = urllib.parse.parse_qs(url.query).get("to", ["http://example.invalid/"])[0]
                self.send(302, b"", extra={"Location": to})
                return
            if url.path == "/garbage":
                self.send(200, b"this is not a time-stamp response", REPLY_TYPE)
                return
            if url.path == "/reject":
                self.send(200, REJECTION, REPLY_TYPE)
                return
            if url.path not in ("/tsr", "/wrongtype", "/slow"):
                self.send(404, b"not found\n")
                return
            if ctype != QUERY_TYPE:
                self.send(415, b"expected application/timestamp-query\n")
                return
            if url.path == "/slow":
                ms = int(urllib.parse.parse_qs(url.query).get("ms", ["3000"])[0])
                time.sleep(ms / 1000.0)
            try:
                reply = tsa.reply(body)
            except Exception as e:  # noqa: BLE001 - любой сбой openssl — 500 с текстом
                self.send(500, f"{e}\n".encode("utf-8"))
                return
            self.send(200, reply, "text/plain" if url.path == "/wrongtype" else REPLY_TYPE)

    return Handler


def main() -> int:
    ap = argparse.ArgumentParser(description="local RFC 3161 TSA for tests")
    ap.add_argument("--pki", default=str(ROOT / "tests" / "fixtures" / "pki"))
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--openssl", default=None)
    ap.add_argument("--lifetime", type=float, default=900.0)
    ap.add_argument("--no-stdin", action="store_true", help="не следить за stdin (ручной запуск)")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()

    pki = Path(a.pki).resolve()
    openssl = find_openssl(a.openssl)
    if not openssl:
        print("tsa_serve: openssl.exe not found", file=sys.stderr)
        return 77
    missing = [n for n in ("tsa.pem", "tsa.key", "chain.pem") if not (pki / n).is_file()]
    if missing or not CONFIG.is_file():
        print(f"tsa_serve: missing {', '.join(missing) or CONFIG}", file=sys.stderr)
        return 77

    work = Path(tempfile.mkdtemp(prefix="ottisk-tsa-"))
    tsa = Tsa(openssl, pki, work, a.verbose)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", a.port), make_handler(tsa))
    server.daemon_threads = True
    print(server.server_address[1], flush=True)

    def stop_later(reason: str) -> None:
        if a.verbose:
            sys.stderr.write(f"tsa: stop ({reason})\n")
        threading.Thread(target=server.shutdown, daemon=True).start()

    if not a.no_stdin:
        def watch_stdin() -> None:
            try:
                sys.stdin.buffer.read()
            except (OSError, ValueError):
                pass
            stop_later("stdin closed")
        threading.Thread(target=watch_stdin, daemon=True).start()
    timer = threading.Timer(a.lifetime, stop_later, args=("lifetime",))
    timer.daemon = True
    timer.start()
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
