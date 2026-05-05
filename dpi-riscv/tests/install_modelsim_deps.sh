#!/bin/bash
#
# install_modelsim_deps.sh — Install 32-bit compatibility libraries for ModelSim ASE.
#
# ModelSim ASE (Altera/Intel Edition) is a 32-bit application. On a 64-bit
# system, it requires 32-bit compatibility libraries to run. This script
# installs them.
#
# Usage:
#   sudo bash tests/install_modelsim_deps.sh
#
# Environment variables:
#   INTELFPGA_DIR  — Path to Intel FPGA installation root (default: $HOME/intelFPGA)
#

set -euo pipefail

echo "============================================"
echo " ModelSim ASE — 32-bit dependency installer"
echo "============================================"
echo ""

# Check we're running as root
if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: This script must be run as root (use sudo)."
    echo "  Usage: sudo bash tests/install_modelsim_deps.sh"
    exit 1
fi

# Check architecture
ARCH=$(dpkg --print-architecture)
if [ "$ARCH" != "amd64" ]; then
    echo "INFO: System architecture is '$ARCH' (not amd64)."
    echo "      32-bit libraries may not be needed."
fi

echo "  [1/3] Enabling i386 architecture..."
dpkg --add-architecture i386
echo "        done"

echo ""
echo "  [2/3] Updating package lists..."
apt-get update -qq
echo "        done"

echo ""
echo "  [3/3] Installing 32-bit compatibility libraries..."
apt-get install -y libc6:i386 lib32stdc++6 libxext6:i386 libxft2:i386 libx11-6:i386 libxrender1:i386 libfontconfig1:i386 libc6-dev:i386 gcc-multilib
echo "        done"

echo ""
echo "============================================"
echo " Verification"
echo "============================================"
echo ""

# Find the ModelSim installation and test vlib
INTELFPGA_DIR="${INTELFPGA_DIR:-$HOME/intelFPGA}"
if [ -d "$INTELFPGA_DIR" ]; then
    latest_ver=$(ls -1 "$INTELFPGA_DIR" 2>/dev/null | grep -E '^[0-9]+\.[0-9]+$' | sort -t. -k1,1nr -k2,2nr | head -1)
    if [ -n "$latest_ver" ]; then
        vsim_path="$INTELFPGA_DIR/$latest_ver/modelsim_ase/linux/vsim"
        if [ -x "$vsim_path" ]; then
            echo "  Testing vsim..."
            if "$vsim_path" -version 2>&1; then
                echo ""
                echo "  SUCCESS: ModelSim is now functional!"
            else
                echo "  WARNING: vsim found but still cannot execute."
            fi
        else
            echo "  WARNING: Could not find vsim at $vsim_path"
        fi
    fi
else
    echo "  WARNING: INTELFPGA_DIR=$INTELFPGA_DIR not found. Skipping verification."
fi

echo ""
echo "============================================"
echo " All done. You can now run:"
echo "   bash tests/run_modelsim.sh"
echo "============================================"
