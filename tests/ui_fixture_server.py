#!/usr/bin/env python3
"""Serve the production UI with deterministic /shorten fixture responses."""

import argparse
import json
import socket
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


def parse_listen(value):
    try:
        host, port_text = value.rsplit(":", 1)
        port = int(port_text)
    except (ValueError, TypeError) as error:
        raise argparse.ArgumentTypeError("listen must be HOST:PORT") from error
    if not host or not 0 < port < 65536:
        raise argparse.ArgumentTypeError("listen must be HOST:PORT")
    return host, port


def make_handler(asset):
    requests = []

    class FixtureHandler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def send_body(self, status, content_type, body):
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = self.path.split("?", 1)[0]
            if path == "/":
                self.send_body(200, "text/html; charset=utf-8", asset)
            elif path == "/favicon.ico":
                self.send_body(204, "image/x-icon", b"")
            elif path == "/__requests":
                body = json.dumps(requests, ensure_ascii=True).encode("utf-8")
                self.send_body(200, "application/json", body)
            elif path == "/fixture-short":
                self.send_body(200, "text/plain; charset=utf-8", b"Fixture short URL opened")
            else:
                self.send_body(404, "text/plain; charset=utf-8", b"Not Found")

        def do_POST(self):
            content_length = self.headers.get("Content-Length", "")
            try:
                length = int(content_length)
            except ValueError:
                length = 0
            body = self.rfile.read(length)
            requests.append(
                {
                    "method": self.command,
                    "path": self.path,
                    "content_type": self.headers.get("Content-Type"),
                    "body": body.decode("utf-8", errors="replace"),
                }
            )

            if self.path != "/shorten":
                self.send_body(404, "text/plain; charset=utf-8", b"Not Found")
                return
            try:
                original_url = json.loads(body)["url"]
            except (KeyError, TypeError, json.JSONDecodeError):
                self.send_body(400, "text/plain; charset=utf-8", b"fixture-invalid-json")
                return

            if original_url == "https://fixture.invalid/http-400":
                self.send_body(400, "text/plain; charset=utf-8", b"fixture-invalid-detail")
            elif original_url == "https://fixture.invalid/http-500":
                self.send_body(500, "text/plain; charset=utf-8", b"fixture-internal-detail")
            elif original_url == "https://fixture.invalid/disconnect":
                self.close_connection = True
                try:
                    self.connection.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                self.connection.close()
            elif original_url == "https://fixture.invalid/malformed-201":
                self.send_body(201, "application/json", b'{"short_url":')
            elif original_url == "https://fixture.invalid/invalid-short-201":
                self.send_body(
                    201,
                    "application/json",
                    json.dumps({"short_url": "javascript:alert(1)"}).encode("utf-8"),
                )
            elif original_url == "https://fixture.invalid/markup-201":
                value = "http://fixture.invalid/<img src=x onerror=alert(1)>"
                self.send_body(
                    201,
                    "application/json",
                    json.dumps({"short_url": value}).encode("utf-8"),
                )
            else:
                host, port = self.server.server_address
                short_url = f"http://{host}:{port}/fixture-short"
                self.send_body(
                    201,
                    "application/json",
                    json.dumps({"short_url": short_url}).encode("utf-8"),
                )

        def log_message(self, format_string, *args):
            print(f"{self.address_string()} - {format_string % args}", flush=True)

    return FixtureHandler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset", required=True, type=Path)
    parser.add_argument("--listen", default="127.0.0.1:8081", type=parse_listen)
    args = parser.parse_args()

    asset = args.asset.read_bytes()
    server = ThreadingHTTPServer(args.listen, make_handler(asset))
    print(f"UI fixture listening on http://{args.listen[0]}:{args.listen[1]}/", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
