#!/bin/bash
#
# install_modelsim.sh — Download and install ModelSim Intel FPGA Starter Edition
#
# This script downloads the free ModelSim Intel FPGA Starter Edition (Linux)
# from Intel's website and installs it to a user-specified directory.
#
# Prerequisites:
#   - wget or curl
#   - libc6, libncurses5, libxft2, libxext6, libxt6, libsm6 (32-bit and 64-bit)
#   - approx 4 GB free disk space
#
# Usage:
#   ./install_modelsim.sh                    # interactive mode
#   ./install_modelsim.sh /opt/modelsim      # install to custom path
#
# After installation, add the ModelSim bin directory to your PATH:
#   export PATH=$PATH:/opt/modelsim/modeltech/linux_x86_64
#

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
DEFAULT_INSTALL_DIR="/opt/intelFPGA/20.1/modelsim_ase"

# Intel Quartus Prime Lite / ModelSim ASE download URL
# This is the free ModelSim Intel FPGA Starter Edition (ASE = Altera Simulation Edition)
# Note: Intel may change the URL. If the download fails, visit:
#   https://www.intel.com/content/www/us/en/software-kit/750666/modelsim-intel-fpgas-starter-edition-software.html
# and download the Linux tarball manually.
MODELSIM_URL="https://downloads.intel.com/akdlm/software/acdsinst/20.1std/711/ib_tar/ModelSimSetup-20.1.0.711-linux.tar"

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# ---------------------------------------------------------------------------
# Check prerequisites
# ---------------------------------------------------------------------------
echo "============================================"
echo " ModelSim Intel FPGA Starter Edition"
echo " Installer"
echo "============================================"
echo ""

# Check for download tool
if command -v wget &>/dev/null; then
    DL_CMD="wget"
elif command -v curl &>/dev/null; then
    DL_CMD="curl -L -O"
else
    echo "ERROR: Neither wget nor curl found. Please install one of them."
    exit 1
fi
echo "Download tool: $DL_CMD"

# Check for required libraries
MISSING_LIBS=0
for lib in libncurses.so.5 libXft.so.2 libXext.so.6 libXt.so.6 libSM.so.6; do
    if ! ldconfig -p | grep -q "$lib"; then
        echo "  WARNING: $lib not found (may be needed by ModelSim)"
    fi
done

echo ""

# ---------------------------------------------------------------------------
# Determine install directory
# ---------------------------------------------------------------------------
INSTALL_DIR="${1:-$DEFAULT_INSTALL_DIR}"

echo "Install directory: $INSTALL_DIR"
echo ""

if [ -d "$INSTALL_DIR/modeltech" ]; then
    echo "ModelSim appears to already be installed at $INSTALL_DIR"
    echo "If you want to reinstall, remove that directory first:"
    echo "  rm -rf $INSTALL_DIR"
    echo ""
    read -rp "Continue anyway? [y/N] " CONFIRM
    if [ "$CONFIRM" != "y" ] && [ "$CONFIRM" != "Y" ]; then
        echo "Aborted."
        exit 0
    fi
fi

# ---------------------------------------------------------------------------
# Download ModelSim
# ---------------------------------------------------------------------------
echo "============================================"
echo " Downloading ModelSim..."
echo "============================================"
echo ""
echo "URL: $MODELSIM_URL"
echo ""

cd "$TMPDIR"

if [ "$DL_CMD" = "wget" ]; then
    wget "$MODELSIM_URL"
else
    curl -L -O "$MODELSIM_URL"
fi

TARBALL="ModelSimSetup-20.1.0.711-linux.tar"
if [ ! -f "$TARBALL" ]; then
    echo "ERROR: Download failed. Please download manually from:"
    echo "  https://www.intel.com/content/www/us/en/software-kit/750666/modelsim-intel-fpgas-starter-edition-software.html"
    exit 1
fi

echo ""
echo "Download complete: $TARBALL"

# ---------------------------------------------------------------------------
# Extract the tarball
# ---------------------------------------------------------------------------
echo ""
echo "============================================"
echo " Extracting..."
echo "============================================"
echo ""

echo "Extracting to $TMPDIR/modelsim_installer..."
mkdir -p modelsim_installer
tar -xf "$TARBALL" -C modelsim_installer
echo "Extraction complete."

# ---------------------------------------------------------------------------
# Run the installer (batch mode)
# ---------------------------------------------------------------------------
echo ""
echo "============================================"
echo " Running ModelSim installer..."
echo "============================================"
echo ""

INSTALLER="$TMPDIR/modelsim_installer/installer"
if [ ! -f "$INSTALLER" ]; then
    # Try to find the setup script
    INSTALLER=$(find "$TMPDIR/modelsim_installer" -name "setup.sh" -o -name "installer" -type f 2>/dev/null | head -1)
fi

if [ -z "$INSTALLER" ] || [ ! -f "$INSTALLER" ]; then
    echo "ERROR: Installer not found in extracted files."
    echo "The tarball may have a different structure. Please install manually."
    echo ""
    echo "Manual installation steps:"
    echo "  1. Extract the tarball: tar -xf $TARBALL"
    echo "  2. Run the setup script"
    echo "  3. Select 'ModelSim Intel FPGA Starter Edition'"
    echo "  4. Choose install path: $INSTALL_DIR"
    exit 1
fi

echo "Running installer (batch mode)..."
echo "Installing to: $INSTALL_DIR"
echo ""
echo "NOTE: The installer may prompt for GUI interaction."
echo "If batch mode fails, run it manually:"
echo "  $INSTALLER"
echo ""

# Try batch/quiet mode first
if "$INSTALLER" --mode unattended --installdir "$INSTALL_DIR" 2>&1; then
    echo ""
    echo "Installation completed successfully."
else
    echo ""
    echo "Batch installation failed. Trying GUI mode..."
    echo "Please follow the on-screen prompts."
    echo ""
    "$INSTALLER" --mode gui
fi

# ---------------------------------------------------------------------------
# Post-installation setup
# ---------------------------------------------------------------------------
echo ""
echo "============================================"
echo " Post-installation"
echo "============================================"
echo ""

MODELSIM_BIN="$INSTALL_DIR/modeltech/linux_x86_64"
if [ -d "$MODELSIM_BIN" ]; then
    echo "ModelSim binaries found at: $MODELSIM_BIN"
    echo ""
    echo "To use ModelSim, add the following to your ~/.bashrc:"
    echo "  export PATH=\$PATH:$MODELSIM_BIN"
    echo ""
    echo "Or run this command now:"
    echo "  export PATH=\$PATH:$MODELSIM_BIN"
    echo ""

    # Verify installation
    if [ -x "$MODELSIM_BIN/vsim" ]; then
        echo "Verification: vsim found at $MODELSIM_BIN/vsim"
        echo "Installation successful!"
    else
        echo "WARNING: vsim not found at expected path."
        echo "You may need to locate the ModelSim binaries manually."
    fi
else
    echo "WARNING: ModelSim binaries not found at expected path:"
    echo "  $MODELSIM_BIN"
    echo ""
    echo "You may need to locate them manually."
    echo "Common locations:"
    echo "  $INSTALL_DIR/modelsim_ase/linux_x86_64"
    echo "  $INSTALL_DIR/modelsim_ae/linux_x86_64"
fi

echo ""
echo "Done."
