#!/bin/node

const fs = require('fs');
const path = require('path');

const { img2Gba } = require('./lib-img2gba');
const jpegDecode = require('./jpeg-decoder');

const imgPath = process.argv[2];
const outDir = process.argv[3];
const binaryMode = process.argv[4] === '--binary';

// make sure directory exists
fs.readdirSync(outDir);

const imgData = fs.readFileSync(imgPath);

const { data, width, height } = jpegDecode(imgData);

const lastSlashIndex = Math.max(imgPath.lastIndexOf('/'), imgPath.lastIndexOf('\\'));
const imgName = imgPath.slice(lastSlashIndex + 1).replace(/\.jpeg$/, '');

// Would be 32 for sprites, but we're just gonna generate
// a tilemap and render as a background
const blockSize = 8;

const { palette, bitmap } = img2Gba(data, width, height, blockSize);

if (binaryMode) {
    // Raw binary: 512 bytes palette (256 x LE uint16) + 16384 bytes bitmap
    const palBuf = Buffer.alloc(512);
    for (let i = 0; i < palette.length; i++) {
        palBuf.writeUInt16LE(palette[i], i * 2);
    }
    const bmpBuf = Buffer.from(bitmap);
    const artPath = path.join(outDir, `${imgName}.art`);
    fs.writeFileSync(artPath, Buffer.concat([palBuf, bmpBuf]));
} else {
    const palettePath = path.join(outDir, `${imgName}.pal.c`);
    const bitmapPath = path.join(outDir, `${imgName}.raw.c`);

    fs.writeFileSync(palettePath, `
extern const unsigned short ${imgName}_Palette[${palette.length}] = {
    ${palette.map(p => `0x${p.toString(16).padStart(4, '0')}`).join(', ')}
};
`);

    fs.writeFileSync(bitmapPath, `
extern const unsigned char ${imgName}_Bitmap[${bitmap.length}] = {
    ${bitmap.map(b => `0x${b.toString(16).padStart(2, '0')}`).join(', ')}
};
`);
}
