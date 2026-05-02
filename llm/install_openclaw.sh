#!/usr/bin/env bash
set -e

# ---- Config ----
REPO_URL="https://github.com/openclaw-ai/openclaw.git"
INSTALL_DIR="$HOME/openclaw"
PYTHON_VERSION="3.10"

echo "[1/6] Installing system dependencies..."
sudo apt update
sudo apt install -y \
    git \
    curl \
    build-essential \
    python${PYTHON_VERSION} \
    python${PYTHON_VERSION}-venv \
    python${PYTHON_VERSION}-dev \
    pipx

echo "[2/6] Cloning OpenClaw..."
rm -rf "$INSTALL_DIR"
git clone "$REPO_URL" "$INSTALL_DIR"
cd "$INSTALL_DIR"

echo "[3/6] Creating virtual environment..."
python${PYTHON_VERSION} -m venv .venv
source .venv/bin/activate

echo "[4/6] Upgrading pip..."
pip install --upgrade pip setuptools wheel

echo "[5/6] Installing Python dependencies..."
if [ -f requirements.txt ]; then
    pip install -r requirements.txt
elif [ -f pyproject.toml ]; then
    pip install .
else
    echo "No dependency file found. Install manually."
fi

echo "[6/6] Setup environment variables..."
cat <<EOF > .env
# Example config
OPENCLAW_MODEL=deepseek-coder
OPENCLAW_API_KEY=your_api_key_here
EOF

echo ""
echo "✅ OpenClaw installed in $INSTALL_DIR"
echo ""
echo "To start:"
echo "  cd $INSTALL_DIR"
echo "  source .venv/bin/activate"
echo "  export \$(cat .env | xargs)"
echo "  python main.py   # or entrypoint defined by repo"