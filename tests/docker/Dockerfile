# Samba server for the docker-labeled test suites (see docker-compose.yml for
# how it is run, and tests/common/SmbFixture.h for how tests talk to it).
#
# Built from a digest-pinned base for reproducibility. To bump: docker pull
# debian:trixie-slim, copy the new digest here, rebuild, and re-run the full
# `docker` label.
FROM debian:trixie-slim@sha256:020c0d20b9880058cbe785a9db107156c3c75c2ac944a6aa7ab59f2add76a7bd

# smbclient is only for the healthcheck (and ad-hoc debugging).
RUN apt-get update \
 && apt-get install -y --no-install-recommends samba smbclient \
 && rm -rf /var/lib/apt/lists/*

# One test user; password "hlmtest" (defaults mirrored in SmbFixture).
RUN useradd -M -s /usr/sbin/nologin hlmtest \
 && printf 'hlmtest\nhlmtest\n' | smbpasswd -a -s hlmtest \
 && mkdir -p /srv/share \
 && chown hlmtest:hlmtest /srv/share

COPY smb.conf /etc/samba/smb.conf

EXPOSE 445

# `docker compose up --wait` blocks on this going healthy.
HEALTHCHECK --interval=2s --timeout=5s --retries=30 \
  CMD smbclient //localhost/share -U hlmtest%hlmtest -c ls >/dev/null 2>&1 || exit 1

CMD ["smbd", "--foreground", "--no-process-group", "--debug-stdout"]
