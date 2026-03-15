#!/bin/bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

rebuild_all=0
for arg in "$@"; do
    case "$arg" in
        --rebuild-all|-a)
            rebuild_all=1
            ;;
    esac
done

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
if [ "$rebuild_all" -eq 1 ]; then
    echo -e "${CYAN}--rebuild-all: reconverting all songs${NC}"
fi

# Step 1: Find fallback art (first .jpg in art/)
fallback_art=""
for f in art/*.jpg; do
    [ -f "$f" ] || continue
    fallback_art="$f"
    break
done
if [ -z "$fallback_art" ]; then
    echo -e "${RED}No .jpg files found in art/ folder. Add your album art there.${NC}"
    exit 1
fi

# Step 2: Convert per-track artwork to GBA tile binary (.art files)
# Each .wav in wavs/ is matched to art/<same name>.jpg; falls back to first .jpg found.
echo ""
echo -e "${GREEN}[1/4] Converting per-track artwork...${NC}"
mkdir -p gsms
for f in wavs/*.wav; do
    [ -f "$f" ] || continue
    name=$(basename "$f" .wav)
    if [ "$rebuild_all" -eq 0 ] && [ -f "gsms/${name}.art" ]; then
        echo -e "  ${CYAN}[skip]${NC} gsms/${name}.art already exists"
        continue
    fi
    art_src="art/${name}.jpg"
    if [ ! -f "$art_src" ]; then
        art_src="$fallback_art"
    fi
    resized="gsms/${name}.jpeg"
    echo "  $art_src -> gsms/${name}.art"
    magick "$art_src" -resize 128x128! "$resized"
    node ./img2gba "$resized" ./gsms --binary
    rm "$resized"
done

# Step 2: Convert .wav files to .gsm at 18157 Hz using SoX two-process pipe
# Pre-processing chain optimizes audio for the GSM codec and 8-bit GBA output:
#   norm → highpass → compand → lowpass → normalize → resample → encode
# - highpass 80: remove sub-bass the GBA speaker can't reproduce (saves codec bits)
# - compand: compress dynamic range so quiet parts survive 8-bit truncation
# - lowpass 8500: anti-alias below the ~9 kHz Nyquist (18157/2)
# - gain -n: final normalization to use full dynamic range
# Modern SoX rejects nonstandard GSM sample rates, so we lie about it in the second process.
echo ""
echo -e "${GREEN}[2/4] Converting .wav to .gsm (18157 Hz, with pre-processing)...${NC}"
mkdir -p gsms
wav_found=0
for f in wavs/*.wav; do
    [ -f "$f" ] || continue
    wav_found=1
    name=$(basename "$f" .wav)
    if [ "$rebuild_all" -eq 0 ] && [ -f "gsms/${name}.gsm" ]; then
        echo -e "  ${CYAN}[skip]${NC} gsms/${name}.gsm already exists"
        continue
    fi
    echo "  $f -> gsms/${name}.gsm"
    sox "$f" -r 18157 -t s16 -c 1 - \
        norm -18 \
        highpass 80 \
        compand 0.3,1 6:-70,-60,-20 -5 -90 0.2 \
        lowpass 8500 \
        gain -n \
        | sox -t s16 -r 8000 -c 1 - "gsms/${name}.gsm"
done
if [ "$wav_found" -eq 0 ]; then
    echo -e "${RED}No .wav files found in wavs/ folder. Add your audio files there.${NC}"
    exit 1
fi

# Step 3: Build gbfs tool (if needed) and pack .gsm and .art files into archive
echo ""
echo -e "${GREEN}[3/4] Packing .gsm and .art files into GBFS archive...${NC}"
if [ ! -f gbfs64/gbfs ]; then
    echo "  Building gbfs tool from source..."
    cc -o gbfs64/gbfs gbfs64/gbfs.c -Wall
fi
gbfs64/gbfs gsmsongs.gbfs gsms/*.gsm gsms/*.art

# Step 4: Build GBA ROM
echo ""
echo -e "${GREEN}[4/4] Building ROM...${NC}"
make -j"$(sysctl -n hw.ncpu)"

echo ""
echo -e "${GREEN}Done! Output: allnewgsm.gba${NC}"
echo "Open it in an emulator (e.g. mGBA) or load it on a flash cart."
