# The walnut-site container's image: nginx with the site built in (walnut-watch builds it on the
# server as walnut-site:<id> from an uploaded site bundle). This file and site.conf stay on the
# server's side: a bundle brings content only. The repository's files are walnut-repo's.
FROM nginx:stable-alpine@sha256:583b7f2fce1cdb8da5ad5c39ebd3c33b98b87d493190e548bdee1fa8c619c311
COPY site.conf /etc/nginx/conf.d/default.conf
COPY site/ /usr/share/nginx/walnut-site/
HEALTHCHECK --interval=5s --timeout=3s --start-period=2s CMD wget -qO /dev/null http://127.0.0.1/health || exit 1
