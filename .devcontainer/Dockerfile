# Use the official devcontainer base image
FROM mcr.microsoft.com/devcontainers/base:noble

# Prevent interactive prompts during apt
ENV DEBIAN_FRONTEND=noninteractive

# Install all system dependencies in a single layer
RUN apt-get update && apt-get install -y --no-install-recommends \
    # RISC-V toolchain
    gcc-riscv64-linux-gnu \
    g++-riscv64-linux-gnu \
    binutils-riscv64-linux-gnu \
    gcc-riscv64-unknown-elf \
    # Simulation tools
    iverilog \
    device-tree-compiler \
    # Build tools for Spike
    git \
    build-essential \
    autoconf \
    automake \
    autotools-dev \
    libmpc-dev \
    libmpfr-dev \
    libgmp-dev \
    gawk \
    bison \
    flex \
    texinfo \
    libtool \
    patchutils \
    bc \
    zlib1g-dev \
    libexpat1-dev \
    # Cleanup
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Build and install Spike (riscv-isa-sim) to /opt/riscv
RUN set -eux; \
    NPROC=$(nproc); \
    PREFIX=/opt/riscv; \
    mkdir -p $PREFIX; \
    cd /tmp; \
    git clone --depth=1 https://github.com/riscv-software-src/riscv-isa-sim.git; \
    cd riscv-isa-sim; \
    mkdir -p build; \
    cd build; \
    ../configure --prefix=$PREFIX; \
    make -j$NPROC; \
    make install; \
    rm -rf /tmp/riscv-isa-sim

# Set RISCV environment variables
ENV RISCV=/opt/riscv
ENV PATH=$RISCV/bin:$PATH

# Restore interactive mode
ENV DEBIAN_FRONTEND=dialog
