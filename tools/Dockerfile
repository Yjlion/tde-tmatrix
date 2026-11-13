# Build, test and screenshot environment for tde-tmatrix:
#   docker build -t tde-tmatrix-build tools/
#   docker run --rm -v "$PWD":/src tde-tmatrix-build /src/tools/build.sh
ARG CODENAME=trixie
FROM debian:${CODENAME}
ARG CODENAME=trixie
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl gnupg \
 && curl -fsSL -o /tmp/trinity-keyring.deb https://mirror.ppa.trinitydesktop.org/trinity/deb/trinity-keyring.deb \
 && dpkg -i /tmp/trinity-keyring.deb && rm /tmp/trinity-keyring.deb \
 && echo "deb http://mirror.ppa.trinitydesktop.org/trinity/deb/trinity-r14.1.x ${CODENAME} main deps" > /etc/apt/sources.list.d/trinity.list \
 && apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config debhelper devscripts equivs fakeroot lintian git \
      tde-cmake tdelibs14-trinity-dev libtqt3-mt-dev tqt3-dev-tools libtqtinterface-dev \
      tdebase-trinity-bin twin-trinity kicker-trinity khelpcenter-trinity \
      xvfb x11-utils x11-xserver-utils xdotool imagemagick dbus-x11 fonts-dejavu fonts-symbola \
      python3 python3-requests python3-venv \
 && rm -rf /var/lib/apt/lists/*
# matrix-sdk needs a newer Rust than Debian ships
ENV RUSTUP_HOME=/opt/rust CARGO_HOME=/opt/cargo PATH=/opt/cargo/bin:$PATH
RUN curl -fsSL https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain stable
# a local homeserver for the tests and screenshots
RUN python3 -m venv /opt/synapse && /opt/synapse/bin/pip install --no-cache-dir matrix-synapse
