#!/usr/bin/env python3
"""Chat between two LoRa boards plugged into this Mac over USB.

Talks the line protocol of the chat firmware over serial and serves a small
web page (index.html) with one chat pane per board.

    ./run.sh                       # scan for ESP32-S3 boards
    ./run.sh --ports /dev/cu.A /dev/cu.B
    ./run.sh --port 8800 --no-browser
"""

import argparse
import json
import os
import queue
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import serial
import serial.tools.list_ports

ESP_VID = 0x303A          # Espressif USB JTAG/serial
BAUD = 115200
SCAN_EVERY = 2.0          # seconds
INFO_RETRY = 1.5          # seconds between INFO requests until the board answers
MAX_TEXT_BYTES = 180
MAX_LOG = 300             # debug lines kept
HERE = os.path.dirname(os.path.abspath(__file__))

STATUS_RANK = {"sending": 0, "sent": 1, "delivered": 2, "read": 3}


def now_ms():
    return int(time.time() * 1000)


def parse_num(s):
    try:
        v = float(s)
    except ValueError:
        return None
    return int(v) if v.is_integer() else v


class Hub:
    """All state lives here, guarded by one lock. Every change is broadcast."""

    def __init__(self):
        self.lock = threading.RLock()
        self.clients = set()      # queue.Queue per SSE client
        self.devices = {}         # id -> {id, port, state, firstSeen, error}
        self.ports = {}           # port -> {port, state: connecting|error, error}
        self.messages = []        # dicts, see send() / on_line()
        self.logs = []
        self.counters = {}        # device id -> last n used
        self.boards = {}          # port -> Board
        self.seq = 0

    # -- broadcasting -----------------------------------------------------
    def _emit(self, kind, data):
        payload = f"event: {kind}\ndata: {json.dumps(data, separators=(',', ':'))}\n\n"
        for q in list(self.clients):
            q.put(payload)

    def snapshot(self):
        return {
            "devices": list(self.devices.values()),
            "ports": list(self.ports.values()),
            "messages": self.messages,
            "logs": self.logs[-MAX_LOG:],
            "maxBytes": MAX_TEXT_BYTES,
        }

    def subscribe(self):
        q = queue.Queue()
        with self.lock:
            q.put(f"event: snapshot\ndata: {json.dumps(self.snapshot(), separators=(',', ':'))}\n\n")
            self.clients.add(q)
        return q

    def unsubscribe(self, q):
        with self.lock:
            self.clients.discard(q)

    # -- devices and ports ------------------------------------------------
    def set_port(self, port, state, error=None):
        with self.lock:
            if state is None:
                if self.ports.pop(port, None) is None:
                    return
            else:
                self.ports[port] = {"port": port, "state": state, "error": error}
            self._emit("ports", list(self.ports.values()))

    def set_device(self, dev_id, **fields):
        with self.lock:
            d = self.devices.get(dev_id)
            if d is None:
                d = {"id": dev_id, "port": None, "state": "connecting",
                     "firstSeen": now_ms(), "error": None}
                self.devices[dev_id] = d
            d.update(fields)
            self._emit("device", d)

    def log(self, port, direction, line):
        with self.lock:
            entry = {"t": now_ms(), "port": port, "dir": direction, "line": line}
            self.logs.append(entry)
            if len(self.logs) > MAX_LOG * 2:
                del self.logs[:MAX_LOG]
            self._emit("log", entry)

    # -- messages ---------------------------------------------------------
    def _new_message(self, **fields):
        self.seq += 1
        m = {"id": self.seq, "from": None, "to": None, "n": None, "text": "",
             "local": False, "sentAt": None, "status": None, "delivery": None,
             "received": False, "receivedAt": None, "rx": None,
             "readLocal": False, "readAt": None}
        m.update(fields)
        self.messages.append(m)
        return m

    def _find_local(self, sender, n, pred=lambda m: True):
        for m in reversed(self.messages):
            if m["local"] and m["from"] == sender and m["n"] == n and pred(m):
                return m
        return None

    def _bump(self, m, status):
        if m["status"] == "failed" and status != "failed":
            m["status"] = status           # a late confirmation beats a timeout
        elif status == "failed":
            if STATUS_RANK.get(m["status"], 0) < STATUS_RANK["delivered"]:
                m["status"] = "failed"
        elif STATUS_RANK[status] > STATUS_RANK.get(m["status"], -1):
            m["status"] = status

    def send(self, dev_id, text):
        with self.lock:
            board = next((b for b in self.boards.values() if b.id == dev_id), None)
            if board is None:
                return None, "Board is not connected"
            if self.devices.get(dev_id, {}).get("error"):
                self.set_device(dev_id, error=None)
            n = self.counters.get(dev_id, 0) % 65535 + 1
            self.counters[dev_id] = n
            m = self._new_message(**{"from": dev_id, "n": n, "text": text, "local": True,
                                     "sentAt": now_ms(), "status": "sending"})
            if not board.write(f"SEND {n} {text}"):
                m["status"] = "failed"
            self._emit("message", m)
            return m, None

    def mark_read(self, dev_id, sender, n):
        with self.lock:
            m = None
            for cand in reversed(self.messages):
                if cand["received"] and cand["to"] == dev_id and cand["from"] == sender and cand["n"] == n:
                    m = cand
                    break
            if m is None:
                return False, "No such message"
            if not m["readLocal"]:
                # Only count it as read once the receipt reaches the board, so
                # the page retries after a replug.
                board = next((b for b in self.boards.values() if b.id == dev_id), None)
                if board is None or board.id is None or not board.write(f"READ {sender} {n}"):
                    return False, "Board is not connected"
                m["readLocal"] = True
                m["readAt"] = now_ms()
                self._emit("message", m)
            return True, None

    def on_line(self, board, line):
        """A protocol line from a board whose ID is known."""
        me = board.id
        parts = line.split(" ")
        cmd = parts[0]
        with self.lock:
            if cmd == "SENT" and len(parts) >= 2:
                m = self._find_local(me, parse_num(parts[1]))
                if m:
                    self._bump(m, "sent")
                    self._emit("message", m)
            elif cmd == "DELIVERED" and len(parts) >= 2:
                m = self._find_local(me, parse_num(parts[1]))
                if m:
                    self._bump(m, "delivered")
                    if len(parts) >= 4:
                        m["delivery"] = {"rssi": parse_num(parts[2]), "snr": parse_num(parts[3])}
                    self._emit("message", m)
            elif cmd == "FAILED" and len(parts) >= 2:
                m = self._find_local(me, parse_num(parts[1]))
                if m:
                    self._bump(m, "failed")
                    self._emit("message", m)
            elif cmd == "READ" and len(parts) >= 3:
                m = self._find_local(me, parse_num(parts[2]))
                if m:
                    self._bump(m, "read")
                    m["readAt"] = m["readAt"] or now_ms()
                    self._emit("message", m)
            elif cmd == "MSG" and len(parts) >= 5:
                sender, n = parts[1], parse_num(parts[2])
                rx = {"rssi": parse_num(parts[3]), "snr": parse_num(parts[4])}
                text = line.split(" ", 5)[5] if len(parts) >= 6 else ""
                t = now_ms()
                # The firmware should drop radio retries, but guard against
                # showing the same message twice.
                for cand in reversed(self.messages):
                    if (cand["received"] and cand["from"] == sender and cand["to"] == me
                            and cand["n"] == n and cand["text"] == text
                            and t - cand["receivedAt"] < 60000):
                        return
                m = self._find_local(sender, n, lambda c: not c["received"])
                if m is None:
                    m = self._new_message(**{"from": sender, "n": n, "text": text})
                m.update({"to": me, "received": True, "receivedAt": t, "rx": rx})
                if m["text"] != text:
                    m["text"] = text
                self._emit("message", m)
            elif cmd == "ERR":
                self.set_device(me, error={"text": line[4:], "t": now_ms()})


class Board:
    """One open serial port. A reader thread parses lines until the port fails."""

    def __init__(self, hub, port):
        self.hub = hub
        self.port = port
        self.id = None
        self.ser = None
        self.wlock = threading.Lock()
        self.alive = True

    def start(self):
        threading.Thread(target=self._run, name=f"board {self.port}", daemon=True).start()

    def write(self, line):
        try:
            with self.wlock:
                self.ser.write((line + "\n").encode("utf-8"))
                self.ser.flush()
        except (serial.SerialException, OSError, AttributeError):
            return False
        self.hub.log(self.port, "tx", line)
        return True

    def _open(self):
        s = serial.Serial()
        s.port = self.port
        s.baudrate = BAUD
        s.dtr = False      # set before open: toggling DTR/RTS resets the board
        s.rts = False
        s.timeout = 0.2
        s.open()
        return s

    def _run(self):
        hub = self.hub
        hub.set_port(self.port, "connecting")
        try:
            self.ser = self._open()
        except (serial.SerialException, OSError) as e:
            hub.set_port(self.port, "error", str(e))
            self._finish(opened=False)
            return
        buf = b""
        last_info = 0.0
        try:
            while self.alive:
                if self.id is None and time.time() - last_info > INFO_RETRY:
                    last_info = time.time()
                    self.write("INFO")
                chunk = self.ser.read(self.ser.in_waiting or 1)
                if not chunk:
                    continue
                buf += chunk
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    line = raw.decode("utf-8", "replace").strip("\r").rstrip()
                    if line:
                        self._handle(line)
        except (serial.SerialException, OSError, TypeError):
            pass   # unplugged (pyserial raises TypeError on some macOS unplugs)
        self._finish(opened=True)

    def _handle(self, line):
        hub = self.hub
        hub.log(self.port, "rx", line)
        parts = line.split(" ")
        if parts[0] == "ID" and len(parts) == 2 and len(parts[1]) == 4:
            new_id = parts[1].upper()
            with hub.lock:
                if self.id and self.id != new_id:
                    hub.set_device(self.id, state="disconnected")
                self.id = new_id
                # A board that moved to another port keeps its history.
                hub.set_device(new_id, port=self.port, state="connected", error=None)
                hub.set_port(self.port, None)
            return
        if self.id is not None:
            hub.on_line(self, line)

    def _finish(self, opened):
        hub = self.hub
        try:
            if self.ser:
                self.ser.close()
        except Exception:
            pass
        with hub.lock:
            if hub.boards.get(self.port) is self:
                del hub.boards[self.port]
            if opened:
                hub.set_port(self.port, None)
            if self.id and hub.devices.get(self.id, {}).get("port") == self.port:
                hub.set_device(self.id, state="disconnected")


def scan_loop(hub, fixed_ports):
    while True:
        if fixed_ports:
            found = [p for p in fixed_ports if os.path.exists(p)]
        else:
            found = [p.device for p in serial.tools.list_ports.comports() if p.vid == ESP_VID]
        with hub.lock:
            # Ports that failed to open are retried on every scan; drop
            # errors for ports that vanished.
            for port in list(hub.ports):
                if port not in found and port not in hub.boards:
                    hub.set_port(port, None)
            for port in found:
                if port not in hub.boards:
                    b = Board(hub, port)
                    hub.boards[port] = b
                    b.start()
        time.sleep(SCAN_EVERY)


def make_handler(hub, http_port):
    allowed_hosts = {f"127.0.0.1:{http_port}", f"localhost:{http_port}"}

    class Handler(BaseHTTPRequestHandler):
        server_version = "lora-chat"

        def log_message(self, fmt, *args):
            pass

        def _host_ok(self):
            # Refuse requests from pages that rebind their DNS name to 127.0.0.1.
            if self.headers.get("Host") not in allowed_hosts:
                self._json(403, {"error": "Forbidden host"})
                return False
            return True

        def _json(self, code, obj):
            body = json.dumps(obj).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if not self._host_ok():
                return
            path = self.path.split("?", 1)[0]
            if path in ("/", "/index.html"):
                with open(os.path.join(HERE, "index.html"), "rb") as f:
                    body = f.read()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(body)
            elif path == "/events":
                self._events()
            else:
                self._json(404, {"error": "Not found"})

        def _events(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.end_headers()
            q = hub.subscribe()
            try:
                while True:
                    try:
                        payload = q.get(timeout=15)
                    except queue.Empty:
                        payload = ": ping\n\n"
                    self.wfile.write(payload.encode("utf-8"))
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass
            finally:
                hub.unsubscribe(q)

        def _body(self):
            try:
                length = int(self.headers.get("Content-Length") or 0)
                if length < 0 or length > 4096:
                    return None
                data = json.loads(self.rfile.read(length) or b"{}")
                return data if isinstance(data, dict) else None
            except (ValueError, UnicodeDecodeError):
                return None

        def do_POST(self):
            if not self._host_ok():
                return
            # Other websites open in the browser may post here. A JSON content type forces a
            # CORS preflight, which this server never answers, and Origin must be this page.
            origin = self.headers.get("Origin")
            ctype = (self.headers.get("Content-Type") or "").split(";")[0].strip()
            if (origin and origin.removeprefix("http://") not in allowed_hosts) or ctype != "application/json":
                return self._json(403, {"error": "Forbidden"})
            data = self._body()
            if data is None:
                return self._json(400, {"error": "Body must be a JSON object"})
            device = str(data.get("device") or "").upper()
            if self.path == "/api/send":
                text = str(data.get("text") or "")
                text = text.replace("\r", " ").replace("\n", " ").strip()
                if not text:
                    return self._json(400, {"error": "Message is empty"})
                if len(text.encode("utf-8")) > MAX_TEXT_BYTES:
                    return self._json(400, {"error": f"Message is longer than {MAX_TEXT_BYTES} bytes"})
                m, err = hub.send(device, text)
                if err:
                    return self._json(409, {"error": err})
                return self._json(200, {"ok": True, "message": m})
            if self.path == "/api/read":
                try:
                    n = int(data.get("n"))
                except (TypeError, ValueError):
                    return self._json(400, {"error": "n must be a number"})
                sender = str(data.get("from") or "").upper()
                ok, err = hub.mark_read(device, sender, n)
                if not ok:
                    return self._json(404 if err == "No such message" else 409, {"error": err})
                return self._json(200, {"ok": True})
            self._json(404, {"error": "Not found"})

    return Handler


def main():
    ap = argparse.ArgumentParser(description="Chat between two LoRa boards over USB.")
    ap.add_argument("--ports", nargs=2, metavar=("A", "B"),
                    help="serial ports to use instead of scanning for ESP32-S3 boards")
    ap.add_argument("--port", type=int, default=8765, help="HTTP port (default 8765)")
    ap.add_argument("--no-browser", action="store_true", help="don't open the browser")
    args = ap.parse_args()

    hub = Hub()
    try:
        httpd = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(hub, args.port))
    except OSError as e:
        sys.exit(f"Can't listen on 127.0.0.1:{args.port}: {e}. Try --port <other>.")
    httpd.daemon_threads = True

    threading.Thread(target=scan_loop, args=(hub, args.ports), name="scan", daemon=True).start()

    url = f"http://127.0.0.1:{args.port}/"
    print(f"LoRa chat running at {url}  (Ctrl+C to stop)", flush=True)
    if args.ports:
        print(f"Using ports {args.ports[0]} and {args.ports[1]}", flush=True)
    else:
        print("Looking for ESP32-S3 boards on USB every 2 s", flush=True)
    if not args.no_browser:
        webbrowser.open(url)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping", flush=True)
    finally:
        httpd.server_close()
        with hub.lock:
            for b in list(hub.boards.values()):
                b.alive = False


if __name__ == "__main__":
    main()
