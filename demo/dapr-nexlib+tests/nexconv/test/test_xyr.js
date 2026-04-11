const { dbglog, dbgwarn, dbgerr } = require("../common/dbglog");
const { ImageIO, ImageUtil } = require('../ImageUtil')
const { RawImage, ZXNimgConv, ZXNimgIO, ZXNimgOps } = require('../ZXNimage');
const FileUtils = require('../common/FileUtils');
const { conv_tileset } = require('../nexconv');

async function run()
{
    const imgdata = await ImageIO.imageDataFromFile('R.png');
    const src = ZXNimgConv.RGBA3forImageData(imgdata);
    const sw = src.width, sh = src.height;
    dbglog('image size:', sw, 'x', sh);

    const dst = new RawImage().create(sw, 8*sh, 'RGBA3');

    for (let i = 0; i < 8; i++) {
        const attr = i << 1;
        const xfed = src.to_xyr_transform(attr);
        ZXNimgOps.blit(xfed, dst, 0, 0, 0, sh*i, sw, sh);
    }

    await dst.write_png('RN.png');

}

run()