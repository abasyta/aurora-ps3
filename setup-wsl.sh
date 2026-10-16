#!/usr/bin/env bash
# ============================================================================
# Aurora Coverflow for PS3 - one-shot build environment setup (WSL/Ubuntu)
# Run inside Ubuntu:  bash setup-wsl.sh
# It installs dependencies, builds the PS3 toolchain + ps3libraries,
# adds a font, and builds aurora.pkg
# ============================================================================
set -e

export PS3DEV="${PS3DEV:-$HOME/ps3dev}"
export PSL1GHT="$PS3DEV"
export PATH="$PATH:$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin"
cd "$(dirname "$0")"

echo "==================================================="
echo " Step 1/5: dependencies"
echo "==================================================="
sudo apt-get update
sudo apt-get install -y build-essential git autoconf automake bison flex \
    libelf-dev make texinfo libncurses5-dev patch wget zlib1g-dev \
    libtool libtool-bin bzip2 libgmp3-dev pkg-config libssl-dev clang \
    python3 python3-setuptools fonts-dejavu-core || true
sudo apt-get install -y python-is-python3 python-dev-is-python3 || true

echo "==================================================="
echo " Step 2/5: PS3 toolchain  (this takes a LONG time: 1-3 hours)"
echo "==================================================="
if [ ! -x "$PS3DEV/ppu/bin/ppu-gcc" ]; then
    sudo mkdir -p "$PS3DEV"
    sudo chown -R "$USER:$USER" "$PS3DEV"
    [ -d "$HOME/ps3toolchain" ] || git clone https://github.com/ps3dev/ps3toolchain.git "$HOME/ps3toolchain"
    cd "$HOME/ps3toolchain"
    ./toolchain.sh
else
    echo "toolchain already installed, skipping"
fi

# persist environment for future shells
if ! grep -q "PS3DEV" "$HOME/.bashrc" 2>/dev/null; then
    cat >> "$HOME/.bashrc" <<EOF
export PS3DEV=$PS3DEV
export PSL1GHT=$PSL1GHT
export PATH=\$PATH:\$PS3DEV/bin:\$PS3DEV/ppu/bin:\$PS3DEV/spu/bin
EOF
fi

echo "==================================================="
echo " Step 3/5: port libraries (cairo + freetype + pixman)"
echo "==================================================="
if [ ! -f "$PS3DEV/portlibs/ppu/lib/libcairo.a" ]; then
    [ -d "$HOME/ps3libraries" ] || git clone https://github.com/ps3dev/ps3libraries.git "$HOME/ps3libraries"
    cd "$HOME/ps3libraries"
    sudo ./getDependencies.sh 2>/dev/null || sudo ./libraries-sudo.sh || true
    ./libraries.sh
else
    echo "ps3libraries already installed, skipping"
fi

echo "==================================================="
echo " Step 4/5: font + icon"
echo "==================================================="
cd "$(dirname "$0")"
mkdir -p usrdir/USRDIR data
if [ -f /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf ]; then
    cp /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf usrdir/USRDIR/font.ttf
else
    echo "WARNING: DejaVuSans.ttf not found; the app will fall back to default fonts"
fi
python3 scripts/gen_icon.py || echo "WARNING: icon generation failed (pkg will use the default icon)"

echo "==================================================="
echo " Step 5/5: building aurora.pkg"
echo "==================================================="
make pkg
echo ""
echo "==================================================="
echo " DONE! Copy aurora.pkg to a USB stick (FAT32) and"
echo " install it from the XMB: Package Manager -> Install Package Files"
echo "==================================================="
ls -lh aurora.pkg