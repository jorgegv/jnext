const { dbglog, dbgwarn, dbgerr } = require("../../nexconv/common/dbglog");
const { conv_layer2, conv_tileset, conv_sprites } = require('../../nexconv/nexconv.js');
const { RawImage, ZXNimgOps } = require("../../nexconv/ZXNimage.js");

async function run()
{
    const img1 = await new RawImage().read_png('./boy.png');
    const img2 = ZXNimgOps.retile(img1, 32, 32, 1, 0);
    await img2.write_png('./boy32.png');

    // await conv_sprites('./boy32.png', './boy32.spalgrp', './boy32');
    await conv_sprites('./boy32.png', null, './boy32');
}

run();
