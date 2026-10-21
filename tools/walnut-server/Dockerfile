# The walnut-repo container's image: nginx with the site built in (walnut-watch builds it on the
# server as walnut-site:<id> from an uploaded site bundle). This file and nginx.conf stay on the
# server's side: a bundle brings content only. www/ (apt, download, licensed) is mounted read-only.
FROM nginx:stable-alpine
COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY site/ /usr/share/nginx/walnut-site/
HEALTHCHECK --interval=5s --timeout=3s --start-period=2s CMD wget -qO /dev/null http://127.0.0.1/health || exit 1
