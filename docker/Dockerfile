# Atari Lynx dev toolchain: cc65 (cl65/ca65/ld65/ar65, target lynx).
# Native arm64 and amd64, built from a pinned cc65 commit.
#
#   scripts/build-toolchain-image.sh   builds this as lynx-dev-toolchain:<N>
ARG DEBIAN_IMAGE=debian:bookworm-slim

FROM ${DEBIAN_IMAGE} AS build
ARG CC65_COMMIT
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
      git ca-certificates build-essential >/dev/null
RUN git init -q /src/cc65 && cd /src/cc65 \
 && git remote add origin https://github.com/cc65/cc65.git \
 && git fetch -q --depth 1 origin ${CC65_COMMIT} && git checkout -q FETCH_HEAD \
 && make -j"$(nproc)" PREFIX=/opt/cc65 >/dev/null 2>&1 \
 && make install PREFIX=/opt/cc65 >/dev/null

FROM ${DEBIAN_IMAGE}
COPY --from=build /opt/cc65 /opt/cc65
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends make python3 >/dev/null \
 && rm -rf /var/lib/apt/lists/*
ENV PATH=/opt/cc65/bin:$PATH
WORKDIR /work
