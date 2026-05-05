#!/bin/bash
#
# uninstall_modelsim_deps.sh — Remove ModelSim ASE and all installed dependencies.
#
# This script reverses the actions of install_modelsim_deps.sh:
#   1. Removes the Intel FPGA / ModelSim installation directory
#   2. Removes 32-bit compatibility libraries installed for ModelSim
#   3. Removes i386 architecture support (if no other i386 packages remain)
#
# Usage:
#   sudo bash tests/uninstall_modelsim_deps.sh
#
# Environment variables:
#   INTELFPGA_DIR  — Path to Intel FPGA installation root
#                    (default: $HOME/intelFPGA, resolved from the original
#                     user when run with sudo)
#

set -euo pipefail

echo "============================================"
echo " ModelSim ASE — Uninstaller"
echo "============================================"
echo ""

# Check we're running as root
if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: This script must be run as root (use sudo)."
    echo "  Usage: sudo bash tests/uninstall_modelsim_deps.sh"
    exit 1
fi

# ====================================================================
# Resolve the original user's home directory
# When run with sudo, $HOME points to /root. We need the actual user.
# ====================================================================
ORIGINAL_USER="${SUDO_USER:-}"
if [ -z "$ORIGINAL_USER" ]; then
    # Not run via sudo — use $HOME as-is
    ORIGINAL_HOME="$HOME"
else
    ORIGINAL_HOME=$(getent passwd "$ORIGINAL_USER" | cut -d: -f6)
fi

echo "  Original user : ${ORIGINAL_USER:-root}"
echo "  Original home : $ORIGINAL_HOME"
echo ""

# ====================================================================
# Step 1: Remove ModelSim installation
# ====================================================================
echo "  [1/4] Removing ModelSim installation..."

# Default: look in the original user's home directory
INTELFPGA_DIR="${INTELFPGA_DIR:-$ORIGINAL_HOME/intelFPGA}"

if [ -d "$INTELFPGA_DIR" ]; then
    echo "        Removing $INTELFPGA_DIR ..."
    rm -rf "$INTELFPGA_DIR"
    echo "        done"
else
    echo "        $INTELFPGA_DIR not found — skipping"
fi

# Also check common alternative locations
for alt_dir in /opt/intelFPGA /usr/local/intelFPGA; do
    if [ -d "$alt_dir" ]; then
        echo "        Removing $alt_dir ..."
        rm -rf "$alt_dir"
        echo "        done"
    fi
done

# ====================================================================
# Step 2: Remove 32-bit compatibility libraries
# ====================================================================
echo ""
echo "  [2/4] Removing 32-bit compatibility libraries..."

# List of packages that install_modelsim_deps.sh installs
PACKAGES=(
    "libc6:i386"
    "lib32stdc++6"
    "libxext6:i386"
    "libxft2:i386"
    "libx11-6:i386"
    "libxrender1:i386"
    "libfontconfig1:i386"
    "libc6-dev:i386"
    "gcc-multilib"
)

# Check which packages are actually installed (exact match on package name)
TO_REMOVE=()
for pkg in "${PACKAGES[@]}"; do
    # dpkg -l shows status + package name; grep for exact match at start of line
    # Status codes: ii = installed, rc = removed-but-config, etc.
    if dpkg -l "$pkg" 2>/dev/null | grep -q "^ii\s\+$pkg\s"; then
        TO_REMOVE+=("$pkg")
        echo "        found installed: $pkg"
    fi
done

if [ ${#TO_REMOVE[@]} -gt 0 ]; then
    # Use dpkg --purge to remove packages regardless of dependency issues.
    # apt-get remove can fail when packages have unmet dependencies (e.g.,
    # libgcc-s1:i386 depending on libc6:i386). dpkg --purge bypasses this.
    echo "        Purging ${#TO_REMOVE[@]} package(s)..."
    dpkg --purge "${TO_REMOVE[@]}"
    echo "        done"
else
    echo "        No ModelSim-related packages found — skipping"
fi

# ====================================================================
# Step 3: Remove i386 architecture (if no other i386 packages remain)
# ====================================================================
echo ""
echo "  [3/4] Checking i386 architecture..."

I386_COUNT=$(dpkg -l 2>/dev/null | grep '^ii.*:i386' | wc -l || true)
if [ "$I386_COUNT" -eq 0 ]; then
    echo "        No i386 packages remain — removing i386 architecture..."
    dpkg --remove-architecture i386
    echo "        done"
else
    echo "        $I386_COUNT i386 package(s) still installed — keeping i386 architecture"
fi

# ====================================================================
# Step 4: Clean up package cache
# ====================================================================
echo ""
echo "  [4/4] Cleaning up package cache..."
apt-get autoremove -y 2>/dev/null || true
apt-get autoclean -y 2>/dev/null || true
echo "        done"

echo ""
echo "============================================"
echo " Uninstall complete."
echo "============================================"
echo ""
echo "The following have been removed:"
echo "  - Intel FPGA / ModelSim installation (~5 GB)"
echo "  - 32-bit compatibility libraries"
echo "  - i386 architecture (if no other i386 packages remain)"
echo ""
echo "To reinstall, run:"
echo "  sudo bash tests/install_modelsim_deps.sh"
