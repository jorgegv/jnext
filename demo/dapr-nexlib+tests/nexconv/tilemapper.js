const { dbglog, dbgwarn, dbgerr } = require("./common/dbglog");
const { ImageIO, ImageUtil } = require('./ImageUtil')
const { RawImage, ZXNimgConv, ZXNimgIO, ZXNimgOps } = require('./ZXNimage');
const FileUtils = require('./common/FileUtils');
const { conv_tileset } = require('./nexconv');

async function analyzeImage8x8(srcC3)
{
    const ni = srcC3.width / 8, nj = srcC3.height / 8, nt = ni*nj;
    dbglog('8x8 tiles:', ni, 'x', nj, '=', nt);

    if (parseFloat(ni) != parseInt(ni) || parseFloat(nj) != parseInt(nj)) {
        dbgerr('error: image dimensions must be multiple of 8');
        return;
    }

    const colC3 = ZXNimgOps.retile(srcC3, 8, 8, 1, 0);
    ZXNimgOps.ip_applyTransparencyThreshold(colC3);

    // dbglog(colC3.width, colC3.height);
    // await colC3.write_png(outpath_prefix + '.col.c3.png');

    const gridTile = new RawImage().create(8, 8, 'RGBA3');


    const uniqueTiles = [];
    const tileIndices = [];
    const attrIndices = [];

    for (let ti = 0; ti < nt; ti++) {
        ZXNimgOps.blit(colC3, gridTile, 0, 8*ti, 0, 0, 8, 8);
        if (0 == uniqueTiles.length) {
            const tileC3 = gridTile.clone();
            uniqueTiles.push(tileC3);
            tileIndices.push(0);
        }
        else {
            let foundIndex = -1;
            for (let ui = 0; ui < uniqueTiles.length; ui++) {
                const candTile = uniqueTiles[ui];
                if (candTile.equals(gridTile)) {
                    foundIndex = ui;
                    break;
                }
            }
            if (foundIndex >= 0) {
                tileIndices.push(foundIndex);
            }
            else {
                tileIndices.push(uniqueTiles.length);
                uniqueTiles.push(gridTile.clone());
            }
        }
        attrIndices.push(0);
    }
    dbglog('unique tile count:', uniqueTiles.length);

    if (uniqueTiles.length > 256) {
        dbgerr('error: unique tile count exceeds 256');
        return;
    }

    return { uniqueTiles, tileIndices, attrIndices, ni, nj, nt };
}

async function analyzeImage8x8_attr(srcC3)
{
    const ni = srcC3.width / 8, nj = srcC3.height / 8, nt = ni*nj;
    dbglog('8x8 tiles:', ni, 'x', nj, '=', nt);

    if (parseFloat(ni) != parseInt(ni) || parseFloat(nj) != parseInt(nj)) {
        dbgerr('error: image dimensions must be multiple of 8');
        return;
    }

    const colC3 = ZXNimgOps.retile(srcC3, 8, 8, 1, 0);
    ZXNimgOps.ip_applyTransparencyThreshold(colC3);

    // dbglog(colC3.width, colC3.height);
    // await colC3.write_png(outpath_prefix + '.col.c3.png');

    const gridTile = new RawImage().create(8, 8, 'RGBA3');


    const uniqueTiles = [];
    const tileIndices = [];
    const attrIndices = [];

    for (let ti = 0; ti < nt; ti++) {
        ZXNimgOps.blit(colC3, gridTile, 0, 8*ti, 0, 0, 8, 8);
        if (0 == uniqueTiles.length) {
            const tileC3 = gridTile.clone();
            uniqueTiles.push(tileC3);
            tileIndices.push(0);
            attrIndices.push(0);
        }
        else {
            let foundIndex = -1;
            let foundAttr  = 0;
            for (let attr = 0; attr < 16; attr += 2) {
                for (let ui = 0; ui < uniqueTiles.length; ui++) {
                    const candTile = uniqueTiles[ui];
                    const xfedTile = candTile.to_xyr_transform(attr);
                    if (xfedTile.equals(gridTile)) {
                        foundIndex = ui;
                        foundAttr = attr;
                        break;
                    }
                }
                if (foundIndex >= 0) break;
            }
            if (foundIndex >= 0) {
                tileIndices.push(foundIndex);
                attrIndices.push(foundAttr);
            }
            else {
                tileIndices.push(uniqueTiles.length);
                attrIndices.push(0);
                uniqueTiles.push(gridTile.clone());
            }
        }
    }
    dbglog('unique tile count:', uniqueTiles.length);

    if (uniqueTiles.length > 256) {
        dbgerr('error: unique tile count exceeds 256');
        return;
    }

    return { uniqueTiles, tileIndices, attrIndices, ni, nj, nt };
}

async function run_tilemapper(inpath_image, outpath_prefix)
{
    dbglog('tilemapper:', inpath_image, '->', outpath_prefix);

    const srcimgdata = await ImageIO.imageDataFromFile(inpath_image);
    const sw = srcimgdata.width, sh = srcimgdata.height;
    dbglog('image size:', sw, 'x', sh);

    const srcC3 = ZXNimgConv.RGBA3forImageData(srcimgdata);

    // const { uniqueTiles, tileIndices, attrIndices, ni, nj, nt } = await analyzeImage8x8(srcC3);
    const { uniqueTiles, tileIndices, attrIndices, ni, nj, nt } = await analyzeImage8x8_attr(srcC3);
    dbglog({ uniqueTiles, tileIndices, attrIndices, ni, nj, nt });

    const tilesetC3 = new RawImage().create(128, 128, 'RGBA3');
    let tsi = 0, tsj = 0;
    for (let ui = 0; ui < uniqueTiles.length; ui++) {
        const tile = uniqueTiles[ui];
        ZXNimgOps.blit(tile, tilesetC3, 0, 0, 8*tsi, 8*tsj, 8, 8);
        tsi++;
        if (tsi >= 16) {
            tsi = 0;
            tsj++;
        }
    }
    await tilesetC3.write_png(outpath_prefix + '.tileset.png');

    let tmi = 0;
    let tmaptxt = '';
    const tmapbin = new Uint8ClampedArray(2*nt);
    for (let ti = 0; ti < nt; ti++) {
        const tileIndex = tileIndices[ti];

        tmapbin[2*ti+0] = tileIndex;
        tmapbin[2*ti+1] = attrIndices[ti];

        let numtxt = ''+tileIndex;
        while (numtxt.length < 3) numtxt = ' ' + numtxt;
        tmaptxt += numtxt + ',';
        tmi++;
        if (tmi >= ni) {
            tmi = 0;
            tmaptxt += '\n';
        }
    }
    await FileUtils.writeFullTextFile(outpath_prefix + '.tilemap.txt', tmaptxt);
    await FileUtils.writeFullBinaryFile(outpath_prefix + '.tilemap.bin', tmapbin);

    await conv_tileset(outpath_prefix + '.tileset.png', outpath_prefix + '.palgrp', outpath_prefix);
}

module.exports = { run_tilemapper };