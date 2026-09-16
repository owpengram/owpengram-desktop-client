#!/usr/bin/env python3
"""Serve a fake OwpenGram release feed, to test self-update without publishing.

The client reads its release feed from OWPENGRAM_UPDATE_URL when that variable
is set, and expects the shape GitHub's "releases/latest" endpoint returns. This
script builds that JSON around a binary you point it at, and serves both the
JSON and the binary over plain HTTP.

Usage:

    python scripts/fake-update-feed.py out/Release/OwpenGram.exe --build 99

Then start the client with the feed pointed at this server:

    # Windows (PowerShell)
    $env:OWPENGRAM_UPDATE_URL = "http://127.0.0.1:8099/latest.json"
    .\\OwpenGram.exe

    # Linux
    OWPENGRAM_UPDATE_URL=http://127.0.0.1:8099/latest.json ./OwpenGram

The client compares --build against the OWPENGRAM_BUILD it was compiled with,
so pass something higher than that. A build compiled with OWPENGRAM_BUILD=0
still works here: the updater stays enabled while a test feed is set.

Nothing here is a "special package" -- the served binary is exactly the file a
GitHub release would carry.
"""
import argparse
import hashlib
import http.server
import json
import os
import socketserver
import sys
import threading


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def build_feed(binary, build, host, notes):
    name = os.path.basename(binary)
    size = os.path.getsize(binary)
    sys.stderr.write('hashing %s (%.1f MB)...\n' % (name, size / 1048576))
    digest = sha256_of(binary)
    return {
        'tag_name': 'O%d' % build,
        'name': 'O%d' % build,
        'draft': False,
        'prerelease': False,
        'body': notes,
        'assets': [{
            'name': name,
            'size': size,
            'browser_download_url': '%s/%s' % (host, name),
            # The client checks this before it stages the download. Drop the
            # field to exercise the "feed carried no digest" warning path.
            'digest': 'sha256:%s' % digest,
        }],
    }


class Handler(http.server.SimpleHTTPRequestHandler):
    feed = b''
    binary_path = ''
    binary_name = ''

    def do_GET(self):
        path = self.path.split('?', 1)[0].lstrip('/')
        if path == 'latest.json':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(self.feed)))
            self.end_headers()
            self.wfile.write(self.feed)
            return
        if path == self.binary_name:
            size = os.path.getsize(self.binary_path)
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(size))
            self.end_headers()
            with open(self.binary_path, 'rb') as handle:
                while True:
                    chunk = handle.read(256 * 1024)
                    if not chunk:
                        break
                    try:
                        self.wfile.write(chunk)
                    except (BrokenPipeError, ConnectionResetError):
                        return
            return
        self.send_error(404)

    def log_message(self, fmt, *args):
        sys.stderr.write('  %s\n' % (fmt % args))


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('binary', help='the build to serve as the update')
    parser.add_argument('--build', type=int, required=True,
        help='release number to advertise, must beat the running one')
    parser.add_argument('--port', type=int, default=8099)
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--notes', default='Local test build.')
    args = parser.parse_args()

    if not os.path.isfile(args.binary):
        parser.error('no such file: %s' % args.binary)

    origin = 'http://%s:%d' % (args.host, args.port)
    feed = build_feed(args.binary, args.build, origin, args.notes)

    Handler.feed = json.dumps(feed, indent=2).encode()
    Handler.binary_path = os.path.abspath(args.binary)
    Handler.binary_name = feed['assets'][0]['name']

    print('serving %s as %s' % (Handler.binary_name, feed['tag_name']))
    print('  sha256 %s' % feed['assets'][0]['digest'][7:])
    print('  feed   %s/latest.json' % origin)
    print()
    print('point the client at it:')
    print('  OWPENGRAM_UPDATE_URL=%s/latest.json' % origin)
    print()
    with Server((args.host, args.port), Handler) as server:
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            print('\nstopped.')


if __name__ == '__main__':
    main()
