#!/bin/bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

if ! command -v yt-dlp &>/dev/null; then
    echo -e "${RED}yt-dlp not found.${NC} Install with: brew install yt-dlp"
    exit 1
fi

if [ $# -eq 0 ]; then
    echo "Usage: ./download.sh <youtube-url-or-playlist>"
    echo ""
    echo "Downloads audio as .wav into wavs/ and thumbnails as .jpg into art/"
    echo "Filenames are matched by video title so go.sh pairs them automatically."
    exit 1
fi

url="$1"

mkdir -p wavs art

echo -e "${GREEN}Downloading audio + thumbnails...${NC}"
echo ""

yt-dlp \
    --yes-playlist \
    --extract-audio \
    --audio-format wav \
    --write-thumbnail \
    --convert-thumbnails jpg \
    --output "wavs/%(title)s.%(ext)s" \
    --output "thumbnail:art/%(title)s.%(ext)s" \
    "$url"

echo ""
echo -e "${GREEN}Done!${NC}"
echo "  wavs/ - $(ls wavs/*.wav 2>/dev/null | wc -l | tr -d ' ') audio files"
echo "  art/  - $(ls art/*.jpg 2>/dev/null | wc -l | tr -d ' ') thumbnails"
echo ""
echo "Run ./go.sh to build the ROM."
