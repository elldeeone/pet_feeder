FROM debian:bookworm-slim

# Download musl cross-compiler for ARM (much smaller static binaries than glibc)
RUN apt-get update && \
    apt-get install -y --no-install-recommends make wget xz-utils ca-certificates && \
    wget -q -O /tmp/musl-cross.tgz \
        "https://musl.cc/arm-linux-musleabi-cross.tgz" && \
    tar xf /tmp/musl-cross.tgz -C /opt && \
    rm /tmp/musl-cross.tgz && \
    rm -rf /var/lib/apt/lists/*

ENV PATH="/opt/arm-linux-musleabi-cross/bin:${PATH}"

WORKDIR /build
COPY . .

CMD ["make", "clean", "all"]
