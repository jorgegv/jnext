const { ImageIO } = require("./ImageUtil");
const { RawImage, ZXNimgIO, ZXNimgConv, ZXNimgOps } = require("./ZXNimage");

electronAPI = require('./common/FileUtils.js');

const color1 = [1,2,5];
const color2 = [2,3,6];

// let PNG = require("pngjs").PNG;
// let fs = require("fs");

// let newfile = new PNG({ width: 10, height: 10 });

// for (let y = 0; y < newfile.height; y++) {
//   for (let x = 0; x < newfile.width; x++) {
//     let idx = (newfile.width * y + x) << 2;

//     let col =
//       (x < newfile.width >> 1) ^ (y < newfile.height >> 1) ? 0xe5 : 0xff;

//     newfile.data[idx] = col;
//     newfile.data[idx + 1] = col;
//     newfile.data[idx + 2] = col;
//     newfile.data[idx + 3] = 0xff;
//   }
// }

// newfile
//   .pack()
//   .pipe(fs.createWriteStream("newfile.png"))
//   .on("finish", function () {
//     console.log("Written!");
//   });


async function run()
{
    const img0 = new RawImage().create(320, 256, 'RGBA8');
    const img1 = new RawImage().create(320, 256, 'IDX8');

    for (let y = 0; y < img0.height; y++) {
        for (let x = 0; x < img0.width; x++) {
            const idx = x + y * img0.width;
            const x0 = (x + 0.5) - 160;
            const y0 = (y + 0.5) - 128;
            let a = Math.atan2(y0, x0);
            while (a < 0) a += 2*Math.PI;
            const lum = (a * 512) / Math.PI;
            const pix = lum & 0x7F;
            const rgba = (255 << 24) | (pix << 16) | (pix << 8) | pix;
            img0.data[idx] = rgba;
            img1.data[idx] = pix;
        }
    }

    await img0.write_png('output_debug/radial.png');
    await ZXNimgIO.saveBinLayer2(img1, 'raw/radial_l2');

    const pal0 = new RawImage().create(1, 256, 'RGB3');
    for (let i = 0; i < 128; i++) {
        let color;
        if (i < 64)
            color = color1[0] | (color1[1] << 3) | (color1[2] << 6);
        else
            color = color2[0] | (color2[1] << 3) | (color2[2] << 6);
        pal0.data[i] = color;
    }

    await ZXNimgIO.saveBinPaletteFile(pal0, 'raw/radial_l2_pal');

    const titleC8 = await ImageIO.imageDataFromFile('../../art/mainmenu/main_title.png');
    const titleC3 = ZXNimgConv.RGBA3forImageData(titleC8);
    const ctables = ZXNimgOps.extractColorTables4(titleC3, 16, 16, null, 15);
    const palimage = ZXNimgOps.retile(ctables.pal, 1, 1, 16, 0);
    const titleI8 = ZXNimgOps.toIndexed8(titleC3, ctables.inv, 255);

    await ZXNimgIO.saveBinLayer2(titleI8, 'raw/main_title_l2');
    await ZXNimgIO.saveBinPaletteFile(palimage, 'raw/main_title_l2_pal');

    for (let i = 0; i < titleI8.data.length; i++) {
        const pix = titleI8.data[i];
        if (pix != 255) {
            img1.data[i] = 128 + pix;
        }
    }

    await ZXNimgIO.saveBinLayer2(img1, 'raw/radial_title_l2');

    for (let i = 0; i < palimage.data.length; i++) {
        pal0.data[128+i] = palimage.data[i];
    }

    await ZXNimgIO.saveBinPaletteFile(pal0, 'raw/radial_title_l2_pal');
}

run();