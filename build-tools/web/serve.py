#!/usr/bin/env python3
"""
HTTP/HTTPS server with COOP/COEP headers for WebGPU.

Usage:
  python serve.py [port]           # HTTP on localhost (local testing)
  python serve.py [port] --ssl     # HTTPS on 0.0.0.0 (remote testing)
"""
import http.server
import socketserver
import ssl
import sys
import os
import subprocess

PORT = 8080
USE_SSL = False

for arg in sys.argv[1:]:
    if arg == "--ssl":
        USE_SSL = True
    elif arg.isdigit():
        PORT = int(arg)

class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Access-Control-Allow-Origin', '*')
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(200)
        self.end_headers()

def generate_cert():
    """Generate self-signed certificate if not exists."""
    if os.path.exists("cert.pem") and os.path.exists("key.pem"):
        return True
    print("Generating self-signed certificate...")
    try:
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "rsa:2048",
            "-keyout", "key.pem", "-out", "cert.pem",
            "-days", "365", "-nodes",
            "-subj", "/CN=localhost"
        ], check=True, capture_output=True)
        return True
    except:
        print("ERROR: openssl not found. Install openssl or use without --ssl")
        return False

if __name__ == "__main__":
    if USE_SSL:
        if not generate_cert():
            sys.exit(1)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain("cert.pem", "key.pem")
        with socketserver.TCPServer(("0.0.0.0", PORT), Handler) as httpd:
            httpd.socket = context.wrap_socket(httpd.socket, server_side=True)
            print(f"HTTPS server at https://<your-ip>:{PORT}")
            print("Accept the self-signed certificate warning in browser")
            try:
                httpd.serve_forever()
            except KeyboardInterrupt:
                print("\nStopped")
    else:
        with socketserver.TCPServer(("127.0.0.1", PORT), Handler) as httpd:
            print(f"HTTP server at http://localhost:{PORT}")
            try:
                httpd.serve_forever()
            except KeyboardInterrupt:
                print("\nStopped")
