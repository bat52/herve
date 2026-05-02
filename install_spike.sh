#!/usr/bin/env bash
set -euo pipefail

# ---- Config ----
PREFIX=/opt/riscv
NPROC=$(nproc)

echo "[*] Installing dependencies..."
sudo apt-get update
sudo apt-get install -y \
    git build-essential autoconf automake autotools-dev \
    libmpc-dev libmpfr-dev libgmp-dev \
    gawk bison flex texinfo \
    libtool patchutils bc zlib1g-dev \
    libexpat1-dev

# Optional but recommended for device tree support
sudo apt-get install -y device-tree-compiler

# ---- Create install dir ----
echo "[*] Creating install directory at $PREFIX"
sudo mkdir -p $PREFIX
sudo chown $USER:$USER $PREFIX

# ---- Build riscv-isa-sim (Spike) ----
echo "[*] Cloning Spike..."
cd /tmp
rm -rf riscv-isa-sim
git clone https://github.com/riscv-software-src/riscv-isa-sim.git
cd riscv-isa-sim

echo "[*] Building Spike..."
mkdir -p build
cd build
../configure --prefix=$PREFIX
make -j$NPROC
make install

# ---- Environment setup ----
if ! grep -q "$PREFIX/bin" ~/.bashrc; then
    echo "[*] Adding $PREFIX/bin to PATH"
    echo "export PATH=$PREFIX/bin:\$PATH" >> ~/.bashrc
fi

export PATH=$PREFIX/bin:$PATH

# ---- Verify ----
echo "[*] Verifying installation..."
if command -v spike >/dev/null 2>&1; then
    echo "[✓] Spike installed successfully"
    spike --help | head -n 5
else
    echo "[✗] Spike installation failed"
    exit 1
fi