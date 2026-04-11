const { dbglog, dbgwarn, dbgerr } = require("./common/dbglog");
const { PathUtil } = require("./common/PathUtil");

const fs = require("fs");
const PNG = require("pngjs").PNG;

const dataURLprefix = 'data:image/EXT;base64,';

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

async function imageDataFromFile(filename)
{
    return new Promise((resolve, reject) => {
        try {
            fs.createReadStream(filename)
            .pipe(
                new PNG({
                    filterType: 4,
                })
            )
            .on("parsed", function () {
                resolve(this);
            });
        }
        catch(err) {
            reject(err);
        }
    });
}

async function writeFileFromImageData(imgdata, filename)
{
    const w = imgdata.width;
    const h = imgdata.height;
    return new Promise((resolve, reject) => {
        try {
            const newfile = new PNG({ width: w, height: h });

            for (let y = 0; y < h; y++) {
                for (let x = 0; x < w; x++) {
                    let idx = (w * y + x) << 2;
                    newfile.data[idx + 0] = imgdata.data[idx + 0];
                    newfile.data[idx + 1] = imgdata.data[idx + 1];
                    newfile.data[idx + 2] = imgdata.data[idx + 2];
                    newfile.data[idx + 3] = imgdata.data[idx + 3];
                }
            }
            
            newfile
                .pack()
                .pipe(fs.createWriteStream(filename))
                .on("finish", function () {
                    resolve();
                });
            
        }
        catch(err) {
            reject(err);
        }
    });    
}

const ImageIO = {
    imageDataFromFile,
    writeFileFromImageData,
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

async function imageFromDataURL(dataURL)
{
    return new Promise((resolve, reject) => {
        const image = new Image();
        image.src = dataURL;
        image.onload = () => resolve(image);
        image.onerror = () => reject();
    });
}

async function imageFromImageData(imageData)
{
    const dataURL = dataURLfromImageData(imageData);
    return imageFromDataURL(dataURL);
}

function dataURLfromImageData(imageData)
{
    const canvas = document.createElement('canvas');
    canvas.width = imageData.width;
    canvas.height = imageData.height;
    const ctx = canvas.getContext('2d');
    ctx.putImageData(imageData, 0, 0);
    return canvas.toDataURL();
}

const ImageUtil = {
    imageFromImageData, imageFromDataURL, dataURLfromImageData
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


/*
function empty(w, h)
{
    const canvas = document.createElement('canvas');
    canvas.width = w;
    canvas.height = h;
    const ctx = canvas.getContext('2d');
    const imgdata = ctx.createImageData(w, h);
    return imgdata;
}
*/

function empty(w, h)
{
    return {
        colorSpace : 'srgb',
        pixelFormat : 'rgba-unorm8',
        width : w,
        height : h,
        data : new Uint8ClampedArray(4 * w * h)
    }
}

function black(w, h)
{
    const imgdata = empty(w, h);
    const data = imgdata.data;

    const pixcnt = w * h;
    let idx = 0;
    for (let i = 0; i < pixcnt; i++) {
        data[idx++] = 0;
        data[idx++] = 0;
        data[idx++] = 0;
        data[idx++] = 255;
    }
    return imgdata;
}

function random(w, h)
{
    const imgdata = empty(w, h);
    const data = imgdata.data;

    const pixcnt = w * h;
    let idx = 0;
    for (let i = 0; i < pixcnt; i++) {
        data[idx++] = 255 * Math.random();
        data[idx++] = 255 * Math.random();
        data[idx++] = 255 * Math.random();
        data[idx++] = 255;
    }
    return imgdata;
}

function checkers(w, h, dx, dy)
{
    const imgdata = empty(w, h);
    const data = imgdata.data;

    let idx = 0;
    for (let j = 0; j < h; j++) {
        const ej = parseInt(j / dy) & 1;
        for (let i = 0; i < w; i++) {
            const ei = parseInt(i / dx) & 1;
            const val = (ei ^ ej) ? 0xAA : 0x55;
            data[idx++] = val;
            data[idx++] = val;
            data[idx++] = val;
            data[idx++] = 0xFF;
        }
    }
return imgdata;
}

function gradient256()
{
    const w = 256;
    const h = 256;
    const imgdata = empty(w, h);
    const data = imgdata.data;

    let idx = 0;
    for (let j = 0; j < h; j++) {
        for (let i = 0; i < w; i++) {
            data[idx++] = i;
            data[idx++] = j;
            data[idx++] = 255 - ((i+j)>>1);
            data[idx++] = 255;
        }
    }
    return imgdata;
}

const ImageDataCreator = {
    empty, black, random, checkers, gradient256
}

module.exports = { ImageIO, ImageUtil, ImageDataCreator };