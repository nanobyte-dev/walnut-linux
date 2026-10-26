# walnut-upload: the only way in for uploads (GitHub Actions, the dev PC). sshd + rrsync as an
# ordinary user (uid 1000, the owner of /data/nanobyte/Walnut), keys only, every login forced into
# `rrsync -wo -no-del -no-overwrite /incoming`: write-only, never replaces or deletes. The container
# sees nothing of the host but incoming/ and its keys; walnut-watch on the host does the rest.
FROM alpine:3.22
RUN apk add --no-cache openssh-server rsync rrsync \
 && adduser -D -u 1000 -h /home/walnut-deploy -s /bin/sh walnut-deploy \
 && sed -i 's/^walnut-deploy:!/walnut-deploy:*/' /etc/shadow
COPY sshd_config /etc/ssh/sshd_config
USER 1000:1000
EXPOSE 2222
CMD ["/usr/sbin/sshd", "-D", "-e", "-f", "/etc/ssh/sshd_config"]
