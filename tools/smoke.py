#!/usr/bin/env python3
"""Exercise URL entry, rendering, relative links, Back and executable metadata."""
import functools
import http.server
from pathlib import Path
import struct
import subprocess
import threading

from http_fixture import Handler as FixtureHandler

ROOT = Path(__file__).resolve().parents[1]

requests = []


class Handler(FixtureHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        requests.append((self.path, self.request_version, self.headers.get("Host")))
        super().do_GET()

    def log_message(self, *_args):
        pass


def main():
    raw = (ROOT / "build/qdos/qlbrowse").read_bytes()
    wrapped = (ROOT / "build/dist/qlbrowse").read_bytes()
    assert raw[6:8] == b"\x4a\xfb"
    assert raw[-8:-4] == b"XTcc"
    assert wrapped[:18].rstrip(b"\0") == b"]!QDOS File Header"
    assert struct.unpack(">HBBII", wrapped[18:30]) == (
        15, 0, 1, int.from_bytes(raw[-4:], "big"), 0
    )
    assert wrapped[30:] == raw

    handler = functools.partial(Handler, directory=ROOT / "fixture")
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        origin = f"127.0.0.1:{server.server_port}"
        url = f"http://{origin}/start"
        try:
            result = subprocess.run(
                [ROOT / "build/host/qlbrowse"],
                input=(f"{url}\n1\nb\nhttp://{origin}/missing.html\n"
                       f"http://{origin}/bad-chunk\nhttp://{origin}/loop\n"
                       f"http://{origin}/secure\n1\nb\nq\n"),
                capture_output=True, text=True, timeout=15, check=True,
            )
        finally:
            server.shutdown()
            thread.join()

    first = "QL browser test - page one"
    second = "QL browser test - page two"
    assert "[1] Open page two" in result.stdout, result.stdout
    assert "[1] Open page one" in result.stdout, result.stdout
    assert result.stdout.index(first) < result.stdout.index(second) < result.stdout.rindex(first)
    assert f"http://{origin}/pages/second.html" in result.stdout, result.stdout
    assert "HTTP status 404" in result.stdout, result.stdout
    assert "Invalid HTTP chunk terminator." in result.stdout, result.stdout
    assert "Too many HTTP redirects." in result.stdout, result.stdout
    assert "This site redirects to HTTPS." in result.stdout, result.stdout
    assert requests == [
        (path, "HTTP/1.1", origin)
        for path in ("/start", "/pages/index.html", "/pages/second.html",
                     "/pages/index.html", "/missing.html", "/bad-chunk",
                     *(["/loop"] * 6), "/secure", "/pages/second.html",
                     "/pages/index.html")
    ], requests
    print("PASS: QDOS executable metadata, HTTP redirects, chunks, Back and recovery")


if __name__ == "__main__":
    main()
