#!/usr/bin/env python3
"""Exercise URL entry, rendering, relative links, Back and executable metadata."""
import functools
import http.server
from pathlib import Path
import ssl
import struct
import subprocess
import tempfile
import threading
from contextlib import ExitStack, contextmanager

from http_fixture import Handler as FixtureHandler
from https_helper import Handler as HelperHandler, PORT

ROOT = Path(__file__).resolve().parents[1]

requests = []
LONG_QUERY = "?q=" + "x" * 620
LONG_LINK = "/long" + LONG_QUERY
LONG_TARGET = "/long-target" + LONG_QUERY


class Handler(FixtureHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        requests.append((self.path, self.request_version, self.headers.get("Host")))
        if self.path == "/wide":
            self.fixture_response("text/plain", b"Large page accepted\n" + b"x" * 65536)
        elif self.path == "/long-menu":
            self.fixture_response("text/html", f'<a href="{LONG_LINK}">Long link</a>'.encode())
        elif self.path == LONG_LINK:
            self.send_response(302)
            self.send_header("Location", LONG_TARGET)
            self.send_header("Content-Length", "0")
            self.end_headers()
        elif self.path == LONG_TARGET:
            self.fixture_response("text/plain", b"Long destination loaded")
        elif self.path == "/feed.xml":
            self.fixture_response("text/xml", b"""<rss version="2.0"><channel>
<title>Test news feed</title>""" + b"<item><title>Earlier headline</title></item>" * 18
                                  + b"""<item><title>Fixture headline</title>
<link>/article?one=1&amp;two=2</link><description>Fixture summary.</description>
</item></channel></rss>""")
        elif self.path == "/article?one=1&two=2":
            self.fixture_response(
                "text/html; charset=utf-8",
                b"<script>" + b"x" * (300 * 1024) + b"</script>"
                b"<article><h1>Fixture article</h1><p>Readable article text.</p><p>"
                + b"x" * 72 + b" wrap intact</p></article>",
            )
        else:
            super().do_GET()

    def fixture_response(self, media_type, body):
        self.send_response(200)
        self.send_header("Content-Type", media_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args):
        pass


@contextmanager
def running(server):
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield
    finally:
        server.shutdown()
        thread.join()


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
    with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
        cert = Path(directory) / "localhost.pem"
        key = Path(directory) / "localhost-key.pem"
        subprocess.run(
            ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
             "-days", "1", "-subj", "/CN=localhost", "-addext",
             "subjectAltName=DNS:localhost", "-keyout", key, "-out", cert],
            capture_output=True, check=True,
        )
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(cert, key)
        upstream = stack.enter_context(http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler))
        upstream.socket = tls.wrap_socket(upstream.socket, server_side=True)
        stack.enter_context(running(upstream))
        secure_origin = f"localhost:{upstream.server_port}"
        Handler.secure_location = f"https://{secure_origin}/start"

        helper = stack.enter_context(http.server.ThreadingHTTPServer(("127.0.0.1", PORT), HelperHandler))
        helper.tls_context = ssl.create_default_context(cafile=cert)
        stack.enter_context(running(helper))

        server = stack.enter_context(http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler))
        stack.enter_context(running(server))
        origin = f"127.0.0.1:{server.server_port}"
        url = f"http://{origin}/start"
        result = subprocess.run(
            [ROOT / "build/host/qlbrowse"],
            input=(f"{url}\n1\nb\nhttp://{origin}/missing.html\n"
                   f"http://{origin}/bad-chunk\nhttp://{origin}/loop\n1\nb\n"
                   f"http://{origin}/secure\n1\nb\n"
                   f"https://127.0.0.1:{upstream.server_port}/start\n"
                   f"http://{origin}/wide\nhttps://{secure_origin}/feed.xml\nn\n1\nb\n1\nb\nb\n"
                   f"http://{origin}/long-menu\n1\nq\n"),
            capture_output=True, text=True, timeout=15, check=True,
        )

    version = (ROOT / "VERSION").read_text().strip()
    assert f"qlbrowse {version}  http://{origin}/pages/second.html\n[Back] [Next] [Prev]" in result.stdout
    first = "QL browser test - page one"
    second = "QL browser test - page two"
    assert "[1] Open page two" in result.stdout, result.stdout
    assert "[1] Open page one" in result.stdout, result.stdout
    assert result.stdout.index(first) < result.stdout.index(second) < result.stdout.rindex(first)
    assert f"http://{origin}/pages/second.html" in result.stdout, result.stdout
    assert "HTTP status 404" in result.stdout, result.stdout
    assert "Invalid HTTP chunk terminator." in result.stdout, result.stdout
    assert "Too many HTTP redirects." in result.stdout, result.stdout
    assert f"https://{secure_origin}/pages/second.html" in result.stdout, result.stdout
    assert "HTTPS helper could not load the remote page." in result.stdout, result.stdout
    assert result.stdout.count("Large page accepted") == 2, result.stdout
    assert result.stdout.count("[1] Fixture headline\nFixture summary.") == 3, result.stdout
    assert "Readable article text." in result.stdout, result.stdout
    assert "x" * 72 + " wrap\nintact" in result.stdout, result.stdout
    assert result.stdout.count("Test news feed") == 1, result.stdout
    assert "Long destination loaded" in result.stdout, result.stdout
    assert requests == [
        (path, "HTTP/1.1", origin)
        for path in ("/start", "/pages/index.html", "/pages/second.html",
                     "/missing.html", "/bad-chunk",
                     *(["/loop"] * 6), "/pages/second.html",
                     "/secure")
    ] + [
        (path, "HTTP/1.1", secure_origin)
        for path in ("/start", "/pages/index.html", "/pages/second.html")
    ] + [("/wide", "HTTP/1.1", origin)] + [
        (path, "HTTP/1.1", secure_origin)
        for path in ("/feed.xml", "/article?one=1&two=2", "/article?one=1&two=2")
    ] + [
        (path, "HTTP/1.1", origin)
        for path in ("/wide", "/long-menu", LONG_LINK, LONG_TARGET)
    ], requests
    print("PASS: QDOS executable metadata, HTTP redirects, chunks, HTTPS verification, larger pages, RSS, article extraction and cached Back")


if __name__ == "__main__":
    main()
