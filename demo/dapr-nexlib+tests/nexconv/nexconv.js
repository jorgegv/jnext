const { dbglog, dbgwarn, dbgerr } = require("./common/dbglog");
const { ImageIO, ImageUtil } = require('./ImageUtil')
const { RawImage, ZXNimgConv, ZXNimgIO, ZXNimgOps } = require('./ZXNimage');

async function conv_layer2(inpath_layer2, outpath_prefix) {

    const imgdata = await ImageIO.imageDataFromFile(inpath_layer2);
    
    let imgC3ori = ZXNimgConv.RGBA3forImageData(imgdata);
    imgC3ori = imgC3ori.crop(320, 256);

    const imgC3 = ZXNimgOps.transpose(imgC3ori);
    const ctables = ZXNimgOps.extractColorTables4(imgC3, 16, 16);
    const palimage = ZXNimgOps.retile(ctables.pal, 1, 1, 16, 0);
    const imgI8 = ZXNimgOps.toIndexed8(imgC3, ctables.inv, 255);

    await ZXNimgIO.saveBinLayer2(imgI8, outpath_prefix + '_l2');
    await ZXNimgIO.saveBinPaletteFile(palimage, outpath_prefix + '_l2_pal');

    const blks = imgI8.split_into_8kb_blocks();
    for (let i = 0; i < blks.length; i++) {
        await ZXNimgIO.saveBinLayer2(blks[i], outpath_prefix + '_l2_' + i);
    }
}

async function conv_tileset(inpath_tileset, inpath_palgrp, outpath_prefix)
{
    let palgroup = null;
    try { palgroup = await ZXNimgIO.loadTextPalgroup(inpath_palgrp); }
    catch(err) { dbglog("no palgroup available") };

    const imgdata = await ImageIO.imageDataFromFile(inpath_tileset);
    
    const imgC3ori = ZXNimgConv.RGBA3forImageData(imgdata);
    ZXNimgOps.ip_applyTransparencyThreshold(imgC3ori)

    let ctables;
    if (!palgroup) ctables = ZXNimgOps.extractColorTables4(imgC3ori, 8, 8);
    else           ctables = ZXNimgOps.extractColorTablesGrouped4(imgC3ori, 8, 8, palgroup, 15)

    let imgI4;
    if (!palgroup) imgI4 = ZXNimgOps.toIndexed4(imgC3ori, 8, 8, ctables.inv, 15);
    else           imgI4 = ZXNimgOps.toIndexedGrouped4(imgC3ori, 8, 8, palgroup, ctables.inv, 15);

    // create a big column of tiles and crop to max 256 tiles
    imgI4 = ZXNimgOps.retile(imgI4, 8, 8, 1, 0);
    if (imgI4.height > 8*256)
        imgI4.crop(8, 8*256)

    await ZXNimgIO.saveBinTilesetFile(imgI4, outpath_prefix + '_ts');

    await ZXNimgIO.saveBinPaletteFile(ctables.pal, outpath_prefix + '_ts_pal');
}

async function conv_sprites(inpath_sprites, inpath_palgrp, outpath_prefix)
{
    let palgroup = null;
    try { palgroup = await ZXNimgIO.loadTextPalgroup(inpath_palgrp); }
    catch(err) { dbglog("no palgroup available") };

    const imgdata = await ImageIO.imageDataFromFile(inpath_sprites);

    const imgC3ori = ZXNimgConv.RGBA3forImageData(imgdata);

    ZXNimgOps.ip_applyTransparencyThreshold(imgC3ori)

    let ctables;
    if (!palgroup) ctables = ZXNimgOps.extractColorTables4(imgC3ori, 16, 16);
    else           ctables = ZXNimgOps.extractColorTablesGrouped4(imgC3ori, 16, 16, palgroup, 15)

    let imgI4;
    if (!palgroup) imgI4 = ZXNimgOps.toIndexed4(imgC3ori, 16, 16, ctables.inv, 15);
    else           imgI4 = ZXNimgOps.toIndexedGrouped4(imgC3ori, 16, 16, palgroup, ctables.inv, 15);

    // create a big column of tiles and crop to max 128 tiles
    imgI4 = ZXNimgOps.retile(imgI4, 16, 16, 1, 0);
    if (imgI4.height > 16*128)
        imgI4.crop(16, 16*128)

    await ZXNimgIO.saveBinTilesetFile(imgI4, outpath_prefix + '_sp');

    await ZXNimgIO.saveBinPaletteFile(ctables.pal, outpath_prefix + '_sp_pal');

    await ZXNimgIO.saveTextPaletteFile(ctables.pal, outpath_prefix + '_sp_pal.txt');
}

module.exports = {
    conv_layer2, 
    conv_tileset,
    conv_sprites, 
}

