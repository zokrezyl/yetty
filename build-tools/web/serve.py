#!/usr/bin/env python3
"""
Simple HTTP server for yetty web demo.
Serves files with proper CORS and MIME type headers for WebAssembly.

Usage: python serve.py [port] [directory]
"""

import http.server
import socketserver
import sys
import os
from functools import partial

class CORSRequestHandler(http.server.SimpleHTTPRequestHandler):
    """HTTP handler with CORS headers and proper WASM MIME type."""

    def end_headers(self):
        # Add CORS headers for cross-origin isolation (required for SharedArrayBuffer)
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')

        # Cache control for development
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')

        super().end_headers()

    def guess_type(self, path):
        """Override to ensure correct MIME types."""
        mimetype = super().guess_type(path)

        # Ensure correct MIME type for WebAssembly
        if path.endswith('.wasm'):
            return 'application/wasm'
        if path.endswith('.js'):
            return 'application/javascript'

        return mimetype

    def do_OPTIONS(self):
        """Handle CORS preflight requests."""
        self.send_response(200)
        self.end_headers()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    directory = sys.argv[2] if len(sys.argv) > 2 else '.'

    os.chdir(directory)

    handler = partial(CORSRequestHandler, directory=directory)

    with socketserver.TCPServer(("0.0.0.0", port), handler) as httpd:
        print(f"\n  yetty web demo server")
        print(f"  Serving at: http://localhost:{port}/")
        print(f"  Directory:  {os.path.abspath(directory)}")
        print(f"\n  Press Ctrl+C to stop.\n")

        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n  Server stopped.")
            sys.exit(0)


if __name__ == "__main__":
    main()
