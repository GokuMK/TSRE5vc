#!/usr/bin/env python3
"""Logs the activity events Open Rails fires (task simulation 06, phase 5).

Connects to Open Rails' activity events page (the /activityevents
websocket of its web server, ActivityEventsWebpage), answers its first
message so that Open Rails sends each event there instead of opening its
message window (the game does not pause), and writes one JSON line per
event: the game's time of day when it came (/API/TIME), the wall clock,
the event's name and message. Events without a message are not sent by
Open Rails, and one repeating the previous one's name and message neither.

Usage: orts-activity-events.py <out.jsonl> [port]
Runs until Open Rails' server goes away (retrying until it is up first).
"""
import base64
import json
import os
import socket
import struct
import sys
import time
import urllib.request


def game_time(port):
    try:
        with urllib.request.urlopen(f"http://localhost:{port}/API/TIME", timeout=5) as reply:
            return float(reply.read().decode())
    except Exception:
        return -1.0


def connect(port):
    sock = socket.create_connection(("localhost", port), timeout=5)
    key = base64.b64encode(os.urandom(16)).decode()
    sock.sendall((f"GET /activityevents HTTP/1.1\r\nHost: localhost:{port}\r\nUpgrade: websocket\r\n"
                  f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n"
                  f"Sec-WebSocket-Protocol: json\r\n\r\n").encode())
    reply = b""
    while b"\r\n\r\n" not in reply:
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError("closed during the handshake")
        reply += chunk
    head, rest = reply.split(b"\r\n\r\n", 1)
    if b" 101 " not in head.split(b"\r\n", 1)[0]:
        raise ConnectionError(head.decode(errors="replace"))
    return sock, rest


def send_text(sock, text):
    # A client's frames are masked.
    data = text.encode()
    mask = os.urandom(4)
    header = bytes([0x81])
    if len(data) < 126:
        header += bytes([0x80 | len(data)])
    else:
        header += bytes([0x80 | 126]) + struct.pack(">H", len(data))
    sock.sendall(header + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))


def frames(sock, buffer):
    """Text messages as they come (fragments joined); pings answered."""
    message = b""

    def need(count):
        nonlocal buffer
        while len(buffer) < count:
            chunk = sock.recv(65536)
            if not chunk:
                raise ConnectionError("closed")
            buffer += chunk
        taken, buffer = buffer[:count], buffer[count:]
        return taken

    while True:
        first, second = need(2)
        final, opcode = first & 0x80, first & 0x0F
        length = second & 0x7F
        if length == 126:
            length = struct.unpack(">H", need(2))[0]
        elif length == 127:
            length = struct.unpack(">Q", need(8))[0]
        mask = need(4) if second & 0x80 else None
        payload = need(length)
        if mask:
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        if opcode == 0x8:
            return
        if opcode == 0x9:
            sock.sendall(bytes([0x8A, len(payload)]) + payload)
            continue
        if opcode in (0x1, 0x0):
            message += payload
            if final:
                yield message.decode(errors="replace")
                message = b""


def main():
    out_path = sys.argv[1]
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 2150
    sock = None
    for _ in range(600):
        try:
            sock, rest = connect(port)
            break
        except OSError:
            time.sleep(1)
    if sock is None:
        sys.exit("no Open Rails web server")
    sock.settimeout(None)
    with open(out_path, "a") as out:
        answered = False
        try:
            for text in frames(sock, rest):
                try:
                    message = json.loads(text)
                except ValueError:
                    continue
                if not answered:
                    # Any answer: Open Rails then sends events here.
                    send_text(sock, json.dumps({"type": "beep", "data": False}))
                    answered = True
                if message.get("Type") != "event":
                    continue
                out.write(json.dumps({"time": game_time(port), "wall": time.time(),
                                      "name": message.get("Header", ""), "text": message.get("Text", "")}) + "\n")
                out.flush()
        except (ConnectionError, OSError):
            pass


if __name__ == "__main__":
    main()
