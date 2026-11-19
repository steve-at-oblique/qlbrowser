#!/usr/bin/env python3
"""Fetch HTTPS pages for qlbrowse using the Linux host's TLS trust store."""
import argparse
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import re
import ssl
from urllib.parse import urlsplit

PORT = 8787
BODY_LIMIT = 32768
REDIRECTS = {301, 302, 303, 307, 308}
AUTHORITY = re.compile(r"[A-Za-z0-9.-]{1,128}(?::[0-9]{1,5})?")


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        self.request.settimeout(10)
        super().setup()

    def do_GET(self):
        try:
            url = urlsplit(self.path)
            port = url.port
            if (url.scheme != "https" or not AUTHORITY.fullmatch(url.netloc)
                    or port == 0 or "#" in self.path
                    or any(not 33 <= ord(c) <= 126 or c == "\\" for c in self.path)):
                raise ValueError("expected an absolute HTTPS URL")
        except ValueError:
            self.reply(400)
            return

        target = url.path or "/"
        if url.query or "?" in self.path:
            target += "?" + url.query
        upstream = http.client.HTTPSConnection(
            url.hostname, port, timeout=6, context=self.server.tls_context
        )
        try:
            upstream.request("GET", target, headers={
                "Accept-Encoding": "identity",
                "Connection": "close",
                "User-Agent": "qlbrowser/1",
            })
            with upstream.getresponse() as response:
                status = response.status
                fields = {}
                body = b""
                if status in REDIRECTS:
                    locations = [v for k, v in response.getheaders() if k.lower() == "location"]
                    if len(locations) == 1 and self.safe_value(locations[0]):
                        fields["Location"] = locations[0]
                elif status == 200:
                    content_type = response.getheader("Content-Type", "text/html")
                    encoding = response.getheader("Content-Encoding", "identity")
                    if not self.safe_value(content_type) or not self.safe_value(encoding):
                        raise ValueError("invalid HTTPS response headers")
                    # The QL renderer uses only the media type.
                    fields["Content-Type"] = content_type.split(";", 1)[0].strip().lower()
                    fields["Content-Encoding"] = encoding
                    transfer = response.getheader("Transfer-Encoding")
                    if transfer and not response.chunked:
                        raise ValueError("unsupported upstream transfer encoding")
                    body = response.read(BODY_LIMIT + 1)
                    if len(body) > BODY_LIMIT:
                        status, fields, body = 413, {}, b""
                    elif response.length:
                        raise ValueError("incomplete upstream response")
        except (OSError, ValueError, http.client.HTTPException) as exc:
            self.log_error("HTTPS fetch failed for %s: %s", url.hostname, exc)
            self.reply(502)
            return
        finally:
            upstream.close()
        self.reply(status, fields, body)

    @staticmethod
    def safe_value(value):
        return len(value) < 1000 and all(c == "\t" or 32 <= ord(c) <= 126 for c in value)

    def reply(self, status, fields=None, body=b""):
        self.send_response(status)
        for name, value in (fields or {}).items():
            self.send_header(name, value)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        if body:
            self.wfile.write(body)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ca-file", help="optional CA bundle for private HTTPS sites")
    args = parser.parse_args()
    context = ssl.create_default_context()
    if args.ca_file:
        context.load_verify_locations(cafile=args.ca_file)
    with ThreadingHTTPServer(("127.0.0.1", PORT), Handler) as server:
        server.tls_context = context
        print(f"qlbrowser HTTPS helper listening on 127.0.0.1:{PORT}", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
