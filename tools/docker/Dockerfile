# Build environment for the PocketBook B300 SDK (its toolchain is linux/amd64).
FROM --platform=linux/amd64 ubuntu:22.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      p7zip-full cmake make python3 file ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /work
