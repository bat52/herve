#!/bin/bash
#
# install_modelsim.sh — Install ModelSim Intel FPGA Starter Edition
#
# This script guides you through downloading and installing the free
# ModelSim Intel FPGA Starter Edition (Linux).
#
# Since Intel requires accepting a license agreement before downloading,
# this script cannot fully automate the download. Instead, it:
#   1. Checks prerequisites (wget/curl, required libraries)
#   2. Opens the Intel download page in your browser
#   3. Guides you through the manual download
#   4. Installs from the downloaded file
#
# Prerequisites:
#   - wget or curl
#   - 32-bit compatibility libraries (libncurses5, libxft2, libxext6, etc.)
#   - ~4 GB free disk space
#
# Usage:
#   ./install_modelsim.sh                    # install to /opt/intelFPGA/20.1/modelsim_ase
#   ./install_modelsim.sh /opt/modelsim      # install to custom path
#
# After installation, add ModelSim to your PATH:
#   export PATH=$PATH:/opt/intelFPGA/20.1/modelsim_ase/modeltech/linux_x86_64
#

set -uo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
DEFAULT_INSTALL_DIR="/opt/intelFPGA/20.1/modelsim_ase"
DOWNLOAD_URL="https://www.intel.com/content/www/us/en/software-kit/750666/modelsim-intel-fpgas-starter-edition-software.html"

# ---------------------------------------------------------------------------
# Color helpers
# ---------------------------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; }

# ---------------------------------------------------------------------------
# Check prerequisites
# ---------------------------------------------------------------------------
echo "============================================"
echo " ModelSim Intel FPGA Starter Edition"
echo " Installer"
echo "============================================"
echo ""

# Check for download tool
HAS_WGET=0
HAS_CURL=0
if command -v wget &>/dev/null; then
    HAS_WGET=1
    info "wget: found"
fi
if command -v curl &>/dev/null; then
    HAS_CURL=1
    info "curl: found"
fi
if [ "$HAS_WGET" -eq 0 ] && [ "$HAS_CURL" -eq 0 ]; then
    error "Neither wget nor curl found. Please install one of them."
    exit 1
fi

# Check for required 32-bit libraries
info "Checking required libraries..."
MISSING_LIBS=""
for lib in libncurses.so.5 libXft.so.2 libXext.so.6 libXt.so.6 libSM.so.6; do
    if ldconfig -p 2>/dev/null | grep -q "$lib"; then
        echo "  $lib: found"
    else
        echo "  $lib: NOT found"
        MISSING_LIBS="$MISSING_LIBS $lib"
    fi
done

if [ -n "$MISSING_LIBS" ]; then
    warn "Some libraries are missing. ModelSim may not run without them."
    echo ""
    echo "On Ubuntu/Debian, install them with:"
    echo "  sudo apt install libncurses5 libxft2 libxext6 libxt6 libsm6"
    echo ""
    echo "On Fedora/RHEL:"
    echo "  sudo dnf install ncurses-compat-libs libXft libXext libXt libSM"
    echo ""
fi

echo ""

# ---------------------------------------------------------------------------
# Determine install directory
# ---------------------------------------------------------------------------
INSTALL_DIR="${1:-$DEFAULT_INSTALL_DIR}"

info "Install directory: $INSTALL_DIR"
echo ""

if [ -d "$INSTALL_DIR/modeltech" ]; then
    warn "ModelSim appears to already be installed at $INSTALL_DIR"
    echo "If you want to reinstall, remove that directory first:"
    echo "  sudo rm -rf $INSTALL_DIR"
    echo ""
    echo -n "Continue anyway? [y/N] "
    read -r CONFIRM
    if [ "$CONFIRM" != "y" ] && [ "$CONFIRM" != "Y" ]; then
        info "Aborted."
        exit 0
    fi
fi

# ---------------------------------------------------------------------------
# Guide user through download
# ---------------------------------------------------------------------------
echo "============================================"
echo " Download ModelSim"
echo "============================================"
echo ""
echo "Since Intel requires accepting a license agreement, the download"
echo "must be done manually through your web browser."
echo ""
echo "Step 1: Download the ModelSim Intel FPGA Starter Edition installer"
echo ""
echo "  Open this URL in your browser:"
echo "    $DOWNLOAD_URL"
echo ""
echo "  Click the download link for Linux and save the file."
echo "  The file is named something like:"
echo "    ModelSimSetup-20.1.0.711-linux.run"
echo ""
echo "Step 2: Place the downloaded file in this directory:"
echo "    $(pwd)"
echo ""

# Try to open the browser
if command -v xdg-open &>/dev/null; then
    echo -n "Open the download page in your browser now? [y/N] "
    read -r OPEN_BROWSER
    if [ "$OPEN_BROWSER" = "y" ] || [ "$OPEN_BROWSER" = "Y" ]; then
        xdg-open "$DOWNLOAD_URL" 2>/dev/null || true
    fi
elif command -v sensible-browser &>/dev/null; then
    echo -n "Open the download page in your browser now? [y/N] "
    read -r OPEN_BROWSER
    if [ "$OPEN_BROWSER" = "y" ] || [ "$OPEN_BROWSER" = "Y" ]; then
        sensible-browser "$DOWNLOAD_URL" 2>/dev/null || true
    fi
fi

echo ""
echo -n "Enter the full path to the downloaded installer file: "
read -r INSTALLER_FILE

if [ ! -f "$INSTALLER_FILE" ]; then
    error "File not found: $INSTALLER_FILE"
    echo "Please download the installer manually and re-run this script."
    exit 1
fi

info "Found installer: $INSTALLER_FILE"
echo ""

# ---------------------------------------------------------------------------
# Run the installer
# ---------------------------------------------------------------------------
echo "============================================"
echo " Installing ModelSim..."
echo "============================================"
echo ""

# Make sure the installer is executable
chmod +x "$INSTALLER_FILE"

# Create install directory if needed
if [ ! -d "$INSTALL_DIR" ]; then
    info "Creating install directory: $INSTALL_DIR"
    mkdir -p "$INSTALL_DIR" 2>/dev/null || sudo mkdir -p "$INSTALL_DIR"
fi

echo ""
info "Running the ModelSim installer..."
echo ""
echo "The installer will open a GUI window."
echo "Follow these steps in the installer:"
echo "  1. Accept the license agreement"
echo "  2. Select 'ModelSim Intel FPGA Starter Edition'"
echo "  3. Set install path to: $INSTALL_DIR"
echo "  4. Complete the installation"
echo ""

# Run the installer
if command -v sudo &>/dev/null && [ ! -w "$INSTALL_DIR" ]; then
    sudo "$INSTALLER_FILE" --mode gui
else
    "$INSTALLER_FILE" --mode gui
fi

echo ""

# ---------------------------------------------------------------------------
# Post-installation setup
# ---------------------------------------------------------------------------
echo "============================================"
echo " Post-installation"
echo "============================================"
echo ""

# Try to find the ModelSim binaries
MODELSIM_BIN=""
for candidate in \
    "$INSTALL_DIR/modeltech/linux_x86_64" \
    "$INSTALL_DIR/modelsim_ase/linux_x86_64" \
    "$INSTALL_DIR/modelsim_ae/linux_x86_64"; do
    if [ -d "$candidate" ]; then
        MODELSIM_BIN="$candidate"
        break
    fi
done

if [ -n "$MODELSIM_BIN" ]; then
    info "ModelSim binaries found at: $MODELSIM_BIN"
    echo ""
    echo "To use ModelSim, add the following to your ~/.bashrc:"
    echo "  export PATH=\$PATH:$MODELSIM_BIN"
    echo ""
    echo "Or run this command now:"
    echo "  export PATH=\$PATH:$MODELSIM_BIN"
    echo ""

    if [ -x "$MODELSIM_BIN/vsim" ]; then
        info "Verification: vsim found at $MODELSIM_BIN/vsim"
        info "Installation successful!"
    else
        warn "vsim not found at expected path."
        echo "You may need to locate the ModelSim binaries manually."
    fi
else
    warn "ModelSim binaries not found at expected paths."
    echo "Try searching for vsim:"
    echo "  find $INSTALL_DIR -name vsim -type f 2>/dev/null"
    echo ""
    echo "Once found, add its directory to your PATH."
fi

echo ""
info "Done."
