# Step 2 baseline measurement rig for CUBRID fresh-boot sub-10s work.
#
# Purpose: reproduce a single-core, 16GB-buffer, fresh-DB cold boot of
# cub_server N times, capturing wall-clock and the per-phase dump emitted
# by the Step 1 boot_perf_trace instrumentation.
#
# Build (from repo root):
#   docker build -f .omc/dockerfiles/baseline/Dockerfile \
#                -t cubrid-boot-baseline .
#
# Run (1 core, 20GB memory to fit the 16GB buffer + overhead):
#   docker run --rm \
#     --cpus=1 --memory=20g --memory-swap=20g \
#     --cap-add=SYS_ADMIN \
#     -v "$(pwd)/.omc/dockerfiles/baseline/out:/data" \
#     -e BASELINE_RUNS=10 \
#     cubrid-boot-baseline
#
# Output lands in .omc/dockerfiles/baseline/out/baseline-phase-timing.md
# (the markdown the Step 2 plan expects).

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      ninja-build \
      bison \
      flex \
      libncurses5-dev \
      libncursesw5-dev \
      libssl-dev \
      libevent-dev \
      libxml2-dev \
      libcurl4-openssl-dev \
      libbz2-dev \
      libelf-dev \
      zlib1g-dev \
      default-jdk-headless \
      ant \
      git \
      coreutils \
      procps \
      gawk \
      gdb \
      ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ENV CUBRID=/opt/cubrid
ENV CUBRID_DATABASES=/data/databases
ENV LD_LIBRARY_PATH=/opt/cubrid/lib
ENV PATH=/opt/cubrid/bin:$PATH

WORKDIR /build
COPY . /build/cubrid_src

RUN cd /build/cubrid_src \
    && git config --global --add safe.directory /build/cubrid_src || true \
    && mkdir -p /opt/cubrid \
    && ./build.sh -m debug -g ninja -t 64 -p /opt/cubrid build \
    && rm -rf /build/cubrid_src

COPY .omc/dockerfiles/baseline/run-baseline.sh /usr/local/bin/run-baseline.sh
COPY .omc/dockerfiles/baseline/cubrid.conf.template /opt/cubrid/conf/cubrid.conf.template
RUN chmod +x /usr/local/bin/run-baseline.sh

WORKDIR /data
ENTRYPOINT ["/usr/local/bin/run-baseline.sh"]
