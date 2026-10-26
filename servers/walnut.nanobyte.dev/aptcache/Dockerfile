# walnut-aptcache: apt-cacher-ng in front of snapshot.debian.org, for builds (CI and the dev PC).
# snapshot.debian.org throttles bulk downloads (503 TooManyRequests); a pinned snapshot never
# changes, so each file is fetched from it once and served from here after. Reached only through
# walnut-repo's nginx, which passes the pinned snapshot dates and nothing else.
FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends apt-cacher-ng && rm -rf /var/lib/apt/lists/* \
 && rm -f /etc/apt-cacher-ng/security.conf	# admin credentials: no admin page here (root-only file)
COPY acng.conf /etc/apt-cacher-ng/acng.conf
USER 1000:1000
EXPOSE 3142
CMD ["/usr/sbin/apt-cacher-ng", "-c", "/etc/apt-cacher-ng", "ForeGround=1"]
