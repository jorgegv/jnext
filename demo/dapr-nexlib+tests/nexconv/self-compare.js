const { dbglog } = require("./common/dbglog")
const { ImageIO } = require("./ImageUtil");
const { ZXNimgConv, RawImage, ZXNimgOps } = require("./ZXNimage");


async function run()
{
    dbglog('self-compare')

    const img8 = await ImageIO.imageDataFromFile('../../art/fforest0/fforest_tileset.png');
    const img3 = ZXNimgConv.RGBA3forImageData(img8);
    const type = img3.type;
    const w = img3.width;
    const h = img3.height;
    const nx = parseInt(w/8);
    const ny = parseInt(h/8);
    dbglog(type, w, h, nx, ny);

    const tiles = [];

    for (let j = 0; j < ny; j++) {
        const sy = 8 * j;
        for (let i = 0; i < nx; i++) {
            const sx = 8 * i;
            const tile = new RawImage().create(8, 8, 'RGBA3');
            ZXNimgOps.blit(img3, tile, sx, sy, 0, 0, 8, 8);
            tiles.push(tile);
        }
    }

    const dupes = new Set();

    for (let i = 0; i < tiles.length; i++) {
        for (let j = i+1; j < tiles.length; j++) {
            const ti = tiles[i];
            const tj = tiles[j];
            if (0 == ZXNimgOps.compare(ti, tj)) {
                dbglog('tile ' + i + ' is equal to ' + j);
                dupes.add(j);
            }
        }
    }

    dbglog('dupes:', dupes.size);

}

run()