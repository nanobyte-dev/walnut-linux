#!/usr/bin/env python3
"""ccache-relay.py ADDR PORTFILE: ccache's remote storage for the build containers, relayed to
walnut.nanobyte.dev/ccache over HTTPS with the login (env WALNUT_CCACHE_AUTH, user:password).

ccache's HTTP backend speaks plain HTTP only, and the store needs a login to write: this listens on
ADDR (the docker bridge's address: reachable from the build containers on this machine, not from
outside), takes GET/HEAD/PUT/DELETE under /ccache/ and forwards each over one kept-alive HTTPS
connection per thread, adding the login. The port it got is written to PORTFILE. Started and
stopped around a build by tools/ccache-remote.sh. Env WALNUT_CCACHE_URL overrides the store
(default https://walnut.nanobyte.dev/ccache/).
"""
import base64, http.client, http.server, os, socket, sys, threading, urllib.parse

STORE = urllib.parse.urlsplit(os.environ.get("WALNUT_CCACHE_URL", "https://walnut.nanobyte.dev/ccache/"))
AUTH = "Basic " + base64.b64encode(os.environ["WALNUT_CCACHE_AUTH"].encode()).decode()
local = threading.local()


class V4HTTPSConnection(http.client.HTTPSConnection):
	"""IPv4 only: on a machine whose IPv6 route goes nowhere (this happens), every request would
	first wait for the IPv6 connect to time out."""
	def connect(self):
		addr = socket.getaddrinfo(self.host, self.port, socket.AF_INET, socket.SOCK_STREAM)[0][4]
		self.sock = self._context.wrap_socket(socket.create_connection(addr, self.timeout),
		                                      server_hostname=self.host)


class V4HTTPConnection(http.client.HTTPConnection):
	def connect(self):
		addr = socket.getaddrinfo(self.host, self.port, socket.AF_INET, socket.SOCK_STREAM)[0][4]
		self.sock = socket.create_connection(addr, self.timeout)


def connection():
	c = getattr(local, "conn", None)
	if c is None:
		cls = V4HTTPSConnection if STORE.scheme == "https" else V4HTTPConnection
		c = local.conn = cls(STORE.hostname, STORE.port, timeout=60)
	return c


class Relay(http.server.BaseHTTPRequestHandler):
	protocol_version = "HTTP/1.1"

	def log_message(self, *a):
		pass

	def relay(self):
		if not self.path.startswith("/ccache/") or ".." in self.path.split("/"):
			self.send_error(404)
			return
		path = STORE.path.rstrip("/") + self.path[len("/ccache"):]
		n = int(self.headers.get("Content-Length") or 0)
		body = self.rfile.read(n) if n else None
		headers = {"Authorization": AUTH}
		if body is not None:	# only with a body: a GET with "Content-Length: 0" waits 15 s at the proxy
			headers["Content-Length"] = str(n)
		for attempt in (1, 2):	# a kept-alive connection the server closed: once more on a new one
			try:
				c = connection()
				c.request(self.command, path, body=body, headers=headers)
				r = c.getresponse()
				data = r.read()
				break
			except (http.client.HTTPException, OSError):
				local.conn = None
				if attempt == 2:
					self.send_error(502)
					return
		self.send_response(r.status)
		self.send_header("Content-Length", str(len(data)))
		self.end_headers()
		if self.command != "HEAD":
			self.wfile.write(data)

	do_GET = do_HEAD = do_PUT = do_DELETE = relay


def main():
	addr, portfile = sys.argv[1], sys.argv[2]
	srv = http.server.ThreadingHTTPServer((addr, 0), Relay)
	srv.daemon_threads = True
	with open(portfile + ".tmp", "w") as f:
		f.write(str(srv.server_address[1]))
	os.replace(portfile + ".tmp", portfile)
	srv.serve_forever()


if __name__ == "__main__":
	main()
