#!/bin/bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

missing=()

check_dep() {
    if ! command -v "$1" &>/dev/null; then
        missing+=("$1")
        return 1
    fi
    return 0
}

echo -e "${GREEN}Checking dependencies...${NC}"

check_dep magick || true
check_dep node || true
check_dep sox || true

if [ -z "${DEVKITPRO:-}" ] || [ -z "${DEVKITARM:-}" ]; then
    missing+=("devkitPro")
fi

if [ ${#missing[@]} -gt 0 ]; then
    echo -e "${RED}Missing dependencies: ${missing[*]}${NC}"
    echo ""
    for dep in "${missing[@]}"; do
        case "$dep" in
            magick)
                echo -e "  ${YELLOW}magick${NC}: brew install imagemagick"
                ;;
            node)
                echo -e "  ${YELLOW}node${NC}: brew install node (or use nvm)"
                ;;
            sox)
                echo -e "  ${YELLOW}sox${NC}: brew install sox"
                ;;
            devkitPro)
                echo -e "  ${YELLOW}devkitPro${NC}:"
                echo "    1. Download the macOS .pkg from: https://github.com/devkitPro/pacman/releases/tag/v6.0.2"
                echo "    2. Install it: sudo installer -pkg /path/to/devkitpro-pacman-installer.pkg -target /"
                echo "    3. Install GBA tools: sudo dkp-pacman -S gba-dev"
                echo "    4. Add to your shell profile (~/.zshrc):"
                echo "         export DEVKITPRO=/opt/devkitpro"
                echo "         export DEVKITARM=\$DEVKITPRO/devkitARM"
                echo '         export PATH=$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH'
                ;;
        esac
    done
    echo ""
    exit 1
fi

echo -e "${GREEN}All dependencies found.${NC}"

# Step 1: Resize the first .jpg in art/ to 128x128 as album.jpeg
echo ""
echo -e "${GREEN}[1/5] Resizing artwork...${NC}"
art_file=""
for f in art/*.jpg; do
    [ -f "$f" ] || continue
    art_file="$f"
    break
done
if [ -z "$art_file" ]; then
    echo -e "${RED}No .jpg files found in art/ folder. Add your album art there.${NC}"
    exit 1
fi
echo "  $art_file -> art/leopard.jpeg"
magick "$art_file" -resize 128x128! "art/leopard.jpeg"

# Step 2: Convert artwork to GBA tile format (generates src/leopard.pal.c + src/leopard.raw.c)
echo ""
echo -e "${GREEN}[2/5] Converting artwork to GBA tiles...${NC}"
echo "  art/leopard.jpeg -> src/leopard.pal.c + src/leopard.raw.c"
node ./img2gba "art/leopard.jpeg" ./src

# Step 3: Convert .wav files to .gsm at 18157 Hz using SoX two-process pipe
# (modern SoX rejects nonstandard GSM sample rates, so we lie about it)
echo ""
echo -e "${GREEN}[3/5] Converting .wav to .gsm (18157 Hz)...${NC}"
mkdir -p gsms
wav_found=0
for f in wavs/*.wav; do
    [ -f "$f" ] || continue
    wav_found=1
    name=$(basename "$f" .wav)
    echo "  $f -> gsms/${name}.gsm"
    sox "$f" -r 18157 -t s16 -c 1 - | sox -t s16 -r 8000 -c 1 - "gsms/${name}.gsm"
done
if [ "$wav_found" -eq 0 ]; then
    echo -e "${RED}No .wav files found in wavs/ folder. Add your audio files there.${NC}"
    exit 1
fi

# Step 4: Build gbfs tool (if needed) and pack .gsm files into archive
echo ""
echo -e "${GREEN}[4/5] Packing .gsm files into GBFS archive...${NC}"
if [ ! -f gbfs64/gbfs ]; then
    echo "  Building gbfs tool from source..."
    cc -o gbfs64/gbfs gbfs64/gbfs.c -Wall
fi
gbfs64/gbfs gsmsongs.gbfs gsms/*.gsm

# Step 5: Build GBA ROM
echo ""
echo -e "${GREEN}[5/5] Building ROM...${NC}"
make -j"$(sysctl -n hw.ncpu)"

echo ""
echo -e "${GREEN}Done! Output: allnewgsm.gba${NC}"
echo "Open it in an emulator (e.g. mGBA) or load it on a flash cart."
