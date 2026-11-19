#!/usr/bin/env python3
"""Serve the QL browser's plain HTTP hardware fixture."""
import argparse
import functools
import http.server
from pathlib import Path

FIXTURE = Path(__file__).resolve().parents[1] / "fixture"


class Handler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path in ("/start", "/loop", "/secure"):
            location = {
                "/start": "pages/index.html",
                "/loop": "/loop",
                "/secure": "https://example.org/",
            }[self.path]
            self.send_response(302)
            self.send_header("Location", location)
            self.send_header("Content-Length", "0")
            self.end_headers()
        elif self.path == "/pages/index.html":
            self.serve_chunks((FIXTURE / "index.html").read_bytes())
        elif self.path == "/pages/second.html":
            self.path = "/second.html"
            super().do_GET()
        elif self.path == "/bad-chunk":
            self.serve_chunks(None)
        else:
            super().do_GET()

    def serve_chunks(self, page):
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Transfer-Encoding", "chunked")
        self.send_header("Connection", "close")
        self.end_headers()
        if page is None:
            self.wfile.write(b"3\r\nabc!\r\n")
            return
        for offset in range(0, len(page), 37):
            chunk = page[offset:offset + 37]
            self.wfile.write(f"{len(chunk):X};fixture=yes\r\n".encode("ascii"))
            self.wfile.write(chunk + b"\r\n")
        self.wfile.write(b"0\r\nX-Fixture: complete\r\n\r\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()

    Handler.protocol_version = "HTTP/1.1"
    handler = functools.partial(Handler, directory=FIXTURE)
    with http.server.ThreadingHTTPServer((args.bind, args.port), handler) as server:
        print(f"Fixture at http://{args.bind}:{server.server_port}/start", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
