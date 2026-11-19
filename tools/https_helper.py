#!/usr/bin/env python3
"""Fetch HTTPS pages for qlbrowse using the Linux host's TLS trust store."""
import argparse
import http.client
from html import escape
from html.parser import HTMLParser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import re
import ssl
import unicodedata
from urllib.parse import urljoin, urlsplit
import xml.etree.ElementTree as ET

PORT = 8787
BODY_LIMIT = 256 * 1024
SOURCE_LIMIT = 4 * 1024 * 1024
XML_TYPES = {"application/rss+xml", "application/xml", "text/xml"}
PUNCTUATION = str.maketrans({"‘": "'", "’": "'", "“": '"', "”": '"',
                            "–": "-", "—": "-", "…": "...", "\xa0": " "})
REDIRECTS = {301, 302, 303, 307, 308}
AUTHORITY = re.compile(r"[A-Za-z0-9.-]{1,128}(?::[0-9]{1,5})?")


def html_text(text):
    text = " ".join(text.split()).translate(PUNCTUATION)
    return escape(unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode())


class FeedTree(ET.TreeBuilder):
    def doctype(self, name, pubid, system):
        raise ValueError("RSS document contains a DTD")


def rss_page(body, address):
    root = ET.fromstring(body, parser=ET.XMLParser(target=FeedTree()))
    channel = root.find("channel") if root.tag == "rss" else None
    if channel is None:
        return None
    parts = [f'<h1>{html_text(channel.findtext("title", "News feed"))}</h1>']
    for item in channel.findall("item")[:99]:
        title = html_text(item.findtext("title", "").strip() or "Untitled story")
        reference = item.findtext("link", "").strip()
        link = urljoin(address, reference)
        if reference and urlsplit(link).scheme in ("http", "https"):
            title = f'<a href="{escape(link, quote=True)}">{title}</a>'
        parts.append(f"<h2>{title}</h2>")
        summary = html_text(item.findtext("description", ""))
        if summary:
            parts.append(f"<p>{summary}</p>")
    return "\n".join(parts).encode("ascii", "xmlcharrefreplace")


class ArticleText(HTMLParser):
    blocks = {"h1", "h2", "h3", "p", "li", "blockquote"}
    omitted = {"aside", "figure", "footer", "nav", "script", "style", "noscript"}

    def __init__(self):
        super().__init__()
        self.depth = 0
        self.hidden = []
        self.block = None
        self.text = []
        self.parts = []

    def handle_starttag(self, tag, attrs):
        if self.hidden:
            if tag in self.omitted:
                self.hidden.append(tag)
        elif tag == "article":
            self.depth += 1
        elif self.depth and tag in self.omitted:
            self.hidden.append(tag)
        elif self.depth and tag in self.blocks:
            self.finish_block()
            self.block = tag
        elif self.block and tag == "br":
            self.text.append(" ")

    def handle_endtag(self, tag):
        if self.hidden:
            if tag == self.hidden[-1]:
                self.hidden.pop()
        elif self.depth and tag == "article":
            self.finish_block()
            self.depth -= 1
        elif tag == self.block:
            self.finish_block()

    def handle_data(self, data):
        if self.block and not self.hidden:
            self.text.append(data)

    def finish_block(self):
        if self.block:
            text = html_text("".join(self.text))
            if text:
                self.parts.append((self.block, text))
        self.text.clear()
        self.block = None


def article_page(body, content_type):
    charset = re.search(r'charset\s*=\s*["\']?([\w.-]+)', content_type, re.I)
    parser = ArticleText()
    try:
        text = body.decode(charset[1] if charset else "utf-8", "replace")
    except (LookupError, UnicodeError):
        return None
    parser.feed(text)
    parser.close()
    if not {"h1", "p"}.issubset(tag for tag, _ in parser.parts):
        return None
    return "\n".join(f"<{tag}>{text}</{tag}>" for tag, text in parser.parts).encode("ascii")


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
                    media_type = content_type.split(";", 1)[0].strip().lower()
                    fields["Content-Type"] = media_type
                    fields["Content-Encoding"] = encoding
                    transfer = response.getheader("Transfer-Encoding")
                    if transfer and not response.chunked:
                        raise ValueError("unsupported upstream transfer encoding")
                    transformable = (encoding.lower() == "identity" and
                                     (media_type == "text/html" or media_type in XML_TYPES))
                    limit = SOURCE_LIMIT if transformable else BODY_LIMIT
                    body = response.read(limit + 1)
                    if len(body) <= limit:
                        if response.length:
                            raise ValueError("incomplete upstream response")
                        converted = None
                        if transformable and media_type in XML_TYPES:
                            converted = rss_page(body, self.path)
                        elif transformable and len(body) > BODY_LIMIT:
                            converted = article_page(body, content_type)
                        if converted is not None:
                            fields["Content-Type"] = "text/html"
                            body = converted
                    if len(body) > BODY_LIMIT:
                        status, fields, body = 413, {}, b""
        except (OSError, ValueError, ET.ParseError, http.client.HTTPException) as exc:
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
