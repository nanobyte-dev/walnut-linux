# included by every location of repo.conf (and the same three lines in ccache/nginx.conf): what is served from
# apt/, licensed/, download/, status/ is data. Never HTML, never sniffed, and a browser that opens one anyway gets
# an opaque origin without scripts. (nginx drops inherited add_header lines in a location that sets its own, so each
# location includes this file.) test_walnut_watch.py checks that every location does.
default_type application/octet-stream;
types { }
add_header X-Content-Type-Options "nosniff" always;
add_header Content-Security-Policy "default-src 'none'; sandbox" always;
