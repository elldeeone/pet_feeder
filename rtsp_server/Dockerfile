FROM --platform=linux/amd64 debian:bookworm-slim

# The Anyka cross-compiler is 32-bit x86 binaries
RUN dpkg --add-architecture i386 && \
    apt-get update && \
    apt-get install -y --no-install-recommends make unzip libc6:i386 libstdc++6:i386 zlib1g:i386 && \
    rm -rf /var/lib/apt/lists/*

# Copy and extract the toolchain
COPY toolchain/arm-anykav200-crosstool.zip /tmp/
RUN cd /opt && unzip /tmp/arm-anykav200-crosstool.zip && \
    rm /tmp/arm-anykav200-crosstool.zip && \
    chmod -R +x /opt/arm-anykav200-crosstool/usr/bin/

ENV PATH="/opt/arm-anykav200-crosstool/usr/bin:${PATH}"

WORKDIR /build
