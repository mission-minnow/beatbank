FROM debian:bookworm-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        gcc-aarch64-linux-gnu \
        libc6-dev-arm64-cross \
        make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

# rm -f before make forces a clean compile inside the layer — belt-and-suspenders
# with .dockerignore excluding build/, so a stale host dsp.so can never satisfy
# make and ship as the release binary.
RUN rm -f build/aarch64/dsp.so && make aarch64

# Output: build/aarch64/dsp.so
