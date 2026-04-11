const { dbglog, dbgwarn, dbgerr } = require("./common/dbglog");
const { ImageDataCreator, ImageIO } = require("./ImageUtil");
const FileUtils = require('./common/FileUtils');
// const FileUtils = electronAPI;   // for running on Electron renderer

const dataURLprefix = 'data:image/EXT;base64,';

const dbg = false;

// types:
// RGBA8
// RGBA3
// RGB3
// IDX8
// IDX4

// RGBA3 pixel:
// MSB      LSB
// AAABBBGGGRRR

const valid_types = new Set(['RGBA8','RGBA3','RGB3','IDX8','IDX4'])

const c3f8 = (c8) => (c8 >> 5);
const c8f3 = (c8) => ((c8 << 5) | (c8 << 2) | (c8 >> 1));

// function test_c3f8() { for (let c8 = 0; c8 < 256; c8++) dbglog(`c3f8(${c8}) = ${c3f8(c8)}`); }
// test_c3f8();
// function test_c8f3() { for (let c3 = 0; c3 < 8; c3++) dbglog(`c8f3(${c3}) = ${c8f3(c3)}`); }
// test_c8f3();


class RawImage
{
    constructor() {
        this.width = 0;
        this.height = 0;
        this.type = null;   // 'UNINITIALIZED';
        this.data = null;
    }

    check_dimension(value, name) {
        if (typeof(value) != 'number' || isNaN(value))
            throw new Error(name + ' is not a number');
        if (value < 1 || value > 65536)
            throw new Error(name + ' = ' + value + ' out of range [1,63356]');
    }

    create(width, height, type)
    {
        this.check_dimension(width, 'width');
        this.check_dimension(height, 'height');
        if (!valid_types.has(type))
            throw new Error('invalid type ' + type);

        const pixel_count = width * height;
        this.width = width;
        this.height = height;
        this.type = type;
        switch(type) {
            case 'RGBA8':
                this.data = new Uint32Array(pixel_count);
                break;
            case 'RGBA3':
            case 'RGB3':
                this.data = new Uint16Array(pixel_count);
                break;
            case 'IDX8':
            case 'IDX4':
                this.data = new Uint8ClampedArray(pixel_count);
                break;
        }

        return this;
    }

    clone()
    {
        const other = new RawImage().create(this.width, this.height, this.type);
        other.data.set(this.data);
        return other;
    }

    equals(other)
    {
        if (this.width != other.width) return false;
        if (this.height != other.height) return false;
        if (this.type != other.type) return false;
        if (!this.type) return false;

        if (this.data.length != other.data.length) return false;
        for (let i = 0; i < this.data.length; i++) {
            if (this.data[i] != other.data[i])
                return false;
        }
        return true;
    }

    getPixel(x, y)
    {
        return this.data[x + y * this.width];
    }

    split_into_8kb_blocks()
    {
        if (this.type == 'IDX4') {
            const res = [];
            const sz = 16384;
            let start = 0;
            let rem = this.data.length;
            while (rem > 0) {
                let end = start;
                if (rem > sz) end += sz;
                else          end += rem;
                const data = this.data.slice(start, end);
                res.push({type:'IDX4',data:data});
                start += sz;
                rem -= sz;
            }
            return res;
        }
        else if (this.type == 'IDX8') {
            const res = [];
            const sz = 8192;
            let start = 0;
            let rem = this.data.length;
            while (rem > 0) {
                let end = start;
                if (rem > sz) end += sz;
                else          end += rem;
                const data = this.data.slice(start, end);
                res.push({type:'IDX8',data:data});
                start += sz;
                rem -= sz;
            }
            return res;
        }
        return null;
    }

    async read_png(filename)
    {
        const imgdata = await ImageIO.imageDataFromFile(filename);
        const dimg = ZXNimgConv.RGBA8forImageData(imgdata);

        this.width = dimg.width;
        this.height = dimg.height;
        this.type = 'RGBA8';
        this.data = dimg.data;

        return this;
    }

    async write_png(filename)
    {
        let img = this;
        if (img.type == 'RGB3')
            img = ZXNimgConv.RGBA8forRGB3(img);
        else if (img.type == 'RGBA3')
            img = ZXNimgConv.RGBA8forRGBA3(img);
        else if (img.type == 'RGBA8')
            img = img;
        else
            throw new Error('unsupported image type');

        const imgdata = imageDataForRGBA8(img);
        return ImageIO.writeFileFromImageData(imgdata, filename);
    }

    to_xyr_transform(xyr) {
        let isX = xyr & 8;
        let isY = xyr & 4;
        let isR = xyr & 2;
        if (isR) isX = !isX;

        const res = this.clone();
        if (isR) res.ip_transpose();
        if (isX) res.ip_mirrorX();
        if (isY) res.ip_mirrorY();
        return res;
    }

    ip_mirrorX() {
        // dbglog('mirrX');
        const w = this.width;
        const h = this.height;
        const d = this.data;
        for (let j = 0; j < h; j++) {
            for (let i = 0; i < w >> 1; i++) {
                const i2 = w - i - 1;
                const a = d[i+j*w]; d[i+j*w] = d[i2+j*w]; d[i2+j*w] = a;
            }
        }
    }

    ip_mirrorY() {
        // dbglog('mirrY');
        const w = this.width;
        const h = this.height;
        const d = this.data;
        for (let i = 0; i < w; i++) {
            for (let j = 0; j < h >> 1; j++) {
                const j2 = h - j - 1;
                const a = d[i+j*w]; d[i+j*w] = d[i+j2*w]; d[i+j2*w] = a;
            }
        }
    }

    ip_transpose() {
        // dbglog('xpose');
        const w = this.width;
        const h = this.height;
        const d = this.data;
        for (let j = 1; j < h; j++) {
            for (let i = j; i < w; i++) {
                const a = d[i+j*w]; d[i+j*w] = d[j+i*w]; d[j+i*w] = a;
            }
        }
    }

    crop(newWidth, newHeight) {
        this.check_dimension(newWidth, 'newWidth');
        this.check_dimension(newHeight, 'newHeight');

        if (newWidth === this.width && newHeight === this.height) {
            return this;
        }

        let newData;
        const newPixelCount = newWidth * newHeight;

        switch(this.type) {
            case 'RGBA8':
                newData = new Uint32Array(newPixelCount);
                break;
            case 'RGBA3':
            case 'RGB3':
                newData = new Uint16Array(newPixelCount);
                break;
            case 'IDX8':
            case 'IDX4':
                newData = new Uint8ClampedArray(newPixelCount);
                break;
            default:
                throw new Error('Image not initialized or invalid type');
        }

        // Copy data
        const copyWidth = Math.min(this.width, newWidth);
        const copyHeight = Math.min(this.height, newHeight);

        for (let y = 0; y < copyHeight; y++) {
            const srcStart = y * this.width;
            const srcEnd = srcStart + copyWidth;
            const dstStart = y * newWidth;
            
            const rowData = this.data.subarray(srcStart, srcEnd);
            newData.set(rowData, dstStart);
        }

        this.width = newWidth;
        this.height = newHeight;
        this.data = newData;

        return this;
    }

}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//
//     ####### #     # #     #                           ###   #######
//          #   #   #  ##    #     #    #    #   ####     #    #     #
//         #     # #   # #   #     #    ##  ##  #    #    #    #     #
//        #       #    #  #  #     #    # ## #  #         #    #     #
//       #       # #   #   # #     #    #    #  #  ###    #    #     #
//      #       #   #  #    ##     #    #    #  #    #    #    #     #
//     ####### #     # #     #     #    #    #   ####    ###   #######
//
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

async function loadBinPaletteFile(filename)
{
    const raw = await FileUtils.readFullBinaryFile(filename);
    return loadBinPaletteData(raw);
}

function loadBinPaletteData(uint8data)
{
    const raw = uint8data;
    const entrycount = raw.length / 2;

    const img = new RawImage().create(1, entrycount, 'RGB3');
    
    let idx = 0;
    for (let i = 0; i < Math.min(entrycount, 256); i++) {
        const d0 = raw[2*i+0];
        const d1 = raw[2*i+1];
        const r = (d0 >> 5) & 7;
        const g = (d0 >> 2) & 7;
        const b = ((d0 << 1) & 7) | (d1 & 7);
        const v = (r) | (g << 3) | (b << 6);
        img.data[idx++] = v;
    }

    return img;
}

///////////////////////////////////////////////////////////////////////////////

async function saveBinPaletteFile(img, filename, trimTrailingZeros)
{
    if (img.type != 'RGB3')
        throw new Error('Image type should be RGB3, is ' + img.type);

    let entrycount = img.width * img.height;
    if (trimTrailingZeros) {
        let tzcount = 0;
        for (let i = img.data.length - 1; i >= 0; i--) {
            if (img.data[i] == 0)
                tzcount++;
            else
                break;
        }
        entrycount -= tzcount;
    }

    const dstdata = new Uint8ClampedArray(2 * entrycount);

    let didx = 0;
    for (let i = 0; i < entrycount; i++) {
        let v = img.data[i];
        const r = v & 7; v >>=3;
        const g = v & 7; v >>=3;
        const b = v & 7; v >>=3;
        const d0 = (r << 5) | (g << 2) | (b >> 1);
        const d1 = b & 1;
        dstdata[didx++] = d0;
        dstdata[didx++] = d1;
    }

    await FileUtils.writeFullBinaryFile(filename, dstdata);
}

async function saveTextPaletteFile(img, filename)
{
    if (img.type != 'RGB3')
        throw new Error('Image type should be RGB3, is ' + img.type);

    const lines = [];
    for (let i = 0; i < img.data.length; i++) {
        const v = img.data[i];
        const r = (v >> 0) & 7;
        const g = (v >> 3) & 7;
        const b = (v >> 6) & 7;
        lines.push(`${r}${g}${b} `);
    }

    await FileUtils.writeFullTextFile(filename, lines.join('\n'));
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

async function loadBinTilesetFile(filename)
{
    const raw = await FileUtils.readFullBinaryFile(filename);
    const pixelcount = raw.length * 2;

    const dimg = new RawImage().create(8, Math.ceil(pixelcount / 8), 'IDX4');
    let idx = 0;
    for (let byte of raw) {
        const hi = (byte >> 4) & 0x0F;
        const lo = (byte >> 0) & 0x0F;
        dimg.data[idx++] = hi;
        dimg.data[idx++] = lo;
    }

    return dimg;
}

///////////////////////////////////////////////////////////////////////////////

async function saveBinTilesetFile(img, filename)
{
    if (img.type != 'IDX4')
        throw new Error('Image type should be IDX4, is ' + img.type);

    const data = new Uint8ClampedArray(img.data.length / 2);
    let hi, lo;
    for (let i = 0; i < img.data.length; i++) {
        if ((i & 1) == 0) {
            hi = (img.data[i] & 0x0F) << 4;
        }
        else {
            lo = (img.data[i] & 0x0F);
            data[i >> 1] = hi | lo;
        }
    }

    await FileUtils.writeFullBinaryFile(filename, data);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

async function loadBinSprites4File(filename)
{
    const raw = await FileUtils.readFullBinaryFile(filename);
    const pixelcount = raw.length * 2;

    const dimg = new RawImage().create(16, Math.ceil(pixelcount / 16), 'IDX4');
    let idx = 0;
    for (let byte of raw) {
        const hi = (byte >> 4) & 0x0F;
        const lo = (byte >> 0) & 0x0F;
        dimg.data[idx++] = hi;
        dimg.data[idx++] = lo;
    }

    return dimg;
}

///////////////////////////////////////////////////////////////////////////////

async function saveBinSprites4File(img, filename)
{
    if (img.type != 'IDX4')
        throw new Error('Image type should be IDX4, is ' + img.type);

    const data = new Uint8ClampedArray(img.data.length / 2);
    let hi, lo;
    for (let i = 0; i < img.data.length; i++) {
        if ((i & 1) == 0) {
            hi = (img.data[i] & 0x0F) << 4;
        }
        else {
            lo = (img.data[i] & 0x0F);
            data[i >> 1] = hi | lo;
        }
    }

    await FileUtils.writeFullBinaryFile(filename, data);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

async function loadBinPalgroup(filename)
{
    const raw = await FileUtils.readFullBinaryFile(filename);
    const tilecount = raw.length;
    const dimg = new RawImage().create(1, tilecount, 'IDX4');
    for (let i = 0; i < raw.length; i++)
        dimg.data[i] = raw[i];
    return dimg;
}

async function loadTextPalgroup(filepath)
{
    const txt = await FileUtils.readFullTextFile(filepath);
    const tbl = {'0':0,'1':1,'2':2,'3':3,'4':4,'5':5,'6':6,'7':7,
        '8':8,'9':9,'A':10,'B':11,'C':12,'D':13,'E':14,'F':15};
    // dbgerr(tbl);
    // const invtbl = {};
    // for (k in tbl) invtbl[tbl[k]] = k;
    // dbgerr(invtbl);
    const arr = [];
    for (let ch of txt) {
        if (ch in tbl) arr.push(tbl[ch]);
    } 
    const dimg = new RawImage().create(1, arr.length, 'IDX4');
    for (let i = 0; i < arr.length; i++)
        dimg.data[i] = arr[i];
    return dimg;
}

///////////////////////////////////////////////////////////////////////////////

async function saveBinPalgroup(img, filepath) {
    if (img.type != 'IDX4')
        throw new Error('Image type should be IDX4, is ' + img.type);
    await FileUtils.writeFullBinaryFile(filepath, img.data);
}

async function saveTextPalgroup(img, filepath) {
    if (img.type != 'IDX4')
        throw new Error('Image type should be IDX4, is ' + img.type);

    const tbl = {0:'0',1:'1',2:'2',3:'3',4:'4',5:'5',6:'6',7:'7',
        8:'8',9:'9',10:'A',11:'B',12:'C',13:'D',14:'E',15:'F'};

    let txt = '';
    let idx = 1;
    for (let val of img.data) {
        txt += tbl[val];
        if ((idx++ % 16) == 0)
            txt += '\n';
    }

    await FileUtils.writeFullTextFile(filepath, txt);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

async function saveBinLayer2(img, filepath) {
    if (img.type != 'IDX8')
        throw new Error('Image type should be IDX8, is ' + img.type);
    await FileUtils.writeFullBinaryFile(filepath, img.data);
}

///////////////////////////////////////////////////////////////////////////////

const ZXNimgIO = {
    loadBinPaletteData,
    loadBinPaletteFile, saveBinPaletteFile, saveTextPaletteFile,
    loadBinTilesetFile, saveBinTilesetFile,
    loadBinSprites4File, saveBinSprites4File,
    loadBinPalgroup, saveBinPalgroup,
    loadTextPalgroup, saveTextPalgroup,
    saveBinLayer2,
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//
// ####### #     # #     #                          #####
//      #   #   #  ##    #     #    #    #   ####  #     #   ####   #    #  #    #
//     #     # #   # #   #     #    ##  ##  #    # #        #    #  ##   #  #    #
//    #       #    #  #  #     #    # ## #  #      #        #    #  # #  #  #    #
//   #       # #   #   # #     #    #    #  #  ### #        #    #  #  # #  #    #
//  #       #   #  #    ##     #    #    #  #    # #     #  #    #  #   ##   #  #
// ####### #     # #     #     #    #    #   ####   #####    ####   #    #    ##
//
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

function RGBA3forRGBA8(simg)
{
    const dimg = new RawImage().create(simg.width, simg.height, 'RGBA3');

    for (let i = 0; i < dimg.width * dimg.height; i++) {
        let sv = simg.data[i];
        const r8 = sv & 0xFF; sv >>= 8;
        const g8 = sv & 0xFF; sv >>= 8;
        const b8 = sv & 0xFF; sv >>= 8;
        const a8 = sv & 0xFF; sv >>= 8;
        const r3 = c3f8(r8);
        const g3 = c3f8(g8);
        const b3 = c3f8(b8);
        const a3 = c3f8(a8);
        const dv = (r3) | (g3 << 3) | (b3 << 6) | (a3 << 9);
        dimg.data[i] = dv;
    }
    return dimg;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

function RGBA8forRGBA3(simg)
{
    const dimg = new RawImage().create(simg.width, simg.height, 'RGBA8');

    for (let i = 0; i < dimg.width * dimg.height; i++) {
        let sv = simg.data[i];
        const r3 = sv & 0x07; sv >>= 3;
        const g3 = sv & 0x07; sv >>= 3;
        const b3 = sv & 0x07; sv >>= 3;
        const a3 = sv & 0x07; sv >>= 3;
        const r8 = c8f3(r3);
        const g8 = c8f3(g3);
        const b8 = c8f3(b3);
        const a8 = c8f3(a3);
        const dv = (r8) | (g8 << 8) | (b8 << 16) | (a8 << 24);
        dimg.data[i] = dv;
    }
    return dimg;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

function RGB3forRGBA8(simg)
{
    const dimg = new RawImage().create(simg.width, simg.height, 'RGB3');

    for (let i = 0; i < dimg.width * dimg.height; i++) {
        let sv = simg.data[i];
        const r8 = sv & 0xFF; sv >>= 8;
        const g8 = sv & 0xFF; sv >>= 8;
        const b8 = sv & 0xFF; sv >>= 8;
        const r3 = c3f8(r8);
        const g3 = c3f8(g8);
        const b3 = c3f8(b8);
        const dv = (r3) | (g3 << 3) | (b3 << 6);
        dimg.data[i] = dv;
    }
    return dimg;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

function RGBA8forRGB3(simg)
{
    const dimg = new RawImage().create(simg.width, simg.height, 'RGBA8');

    for (let i = 0; i < dimg.width * dimg.height; i++) {
        let sv = simg.data[i];
        const r3 = sv & 0x07; sv >>= 3;
        const g3 = sv & 0x07; sv >>= 3;
        const b3 = sv & 0x07; sv >>= 3;
        const r8 = c8f3(r3);
        const g8 = c8f3(g3);
        const b8 = c8f3(b3);
        const a8 = 255;
        const dv = (r8) | (g8 << 8) | (b8 << 16) | (a8 << 24);
        dimg.data[i] = dv;
    }
    return dimg;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

function RGBA8forImageData(imgdata)
{
    const dimg = new RawImage().create(imgdata.width, imgdata.height, 'RGBA8');

    let sidx = 0;
    let didx = 0;
    for (let i = 0; i < dimg.width * dimg.height; i++) {
        const r = imgdata.data[sidx++];
        const g = imgdata.data[sidx++];
        const b = imgdata.data[sidx++];
        const a = imgdata.data[sidx++];
        const v = (r) | (g << 8) | (b << 16) | (a << 24);
        dimg.data[didx++] = v;
    }

    return dimg;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

function imageDataForRGBA8(simg)
{
    const imgdata = ImageDataCreator.empty(simg.width, simg.height);

    let didx = 0;
    for (let i = 0; i < simg.width * simg.height; i++) {
        let sv = simg.data[i];
        const r8 = sv & 0xFF; sv >>= 8;
        const g8 = sv & 0xFF; sv >>= 8;
        const b8 = sv & 0xFF; sv >>= 8;
        const a8 = sv & 0xFF; sv >>= 8;
        imgdata.data[didx++] = r8;
        imgdata.data[didx++] = g8;
        imgdata.data[didx++] = b8;
        imgdata.data[didx++] = a8;
    }
    return imgdata;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

function RGBA3forImageData(imgdata) {
    return RGBA3forRGBA8(RGBA8forImageData(imgdata));
}

function imageDataForRGBA3(simg) {
    return imageDataForRGBA8(RGBA8forRGBA3(simg));
}

function RGB3forImageData(imgdata) {
    return RGB3forRGBA8(RGBA8forImageData(imgdata));
}

function imageDataForRGB3(simg) {
    return imageDataForRGBA8(RGBA8forRGB3(simg));
}

function RGB3forIDX4(simg, palette, palgroup) {
    if (simg.type != 'IDX4')
        throw new Error('Image type should be IDX4, is ' + simg.type);
    if (palette.type != 'RGB3')
        throw new Error('Palette type should be IDX4, is ' + palette.type);

    const tw = simg.width;

    const dimg = new RawImage().create(simg.width, simg.height, 'RGB3')

    let palbase = 0;
    for (let i = 0; i < simg.data.length; i++) {
        const palidx = simg.data[i];
        if (palgroup) {
            const grpidx = parseInt(i / (tw*tw));
            palbase = palgroup.data[grpidx];
            if (!palbase) palbase = 0;
        }
        let palval = palette.data[16 * palbase + palidx];
        if (!palval) palval = 0;
        dimg.data[i] = palval;
    }

    return dimg;
}

function RGBA3forIDX4(simg, palette, palgroup, transpindex) {
    if (simg.type != 'IDX4')
        throw new Error('Image type should be IDX4, is ' + simg.type);
    if (palette.type != 'RGB3')
        throw new Error('Palette type should be IDX4, is ' + palette.type);

    if (!transpindex && transpindex != 0)
        transpindex = 15;

    const tw = simg.width;

    const dimg = new RawImage().create(simg.width, simg.height, 'RGBA3')

    let palbase = 0;
    for (let i = 0; i < simg.data.length; i++) {
        const palidx = simg.data[i];
        if (palgroup) {
            const grpidx = parseInt(i / (tw*tw));
            palbase = palgroup.data[grpidx];
            if (!palbase) palbase = 0;
        }
        let palval = palette.data[16 * palbase + palidx];
        if (!palval) palval = 0;
        if (palidx != transpindex)
            palval |= (7 << 9);
        dimg.data[i] = palval;
    }

    return dimg;
}

const ZXNimgConv = {
    RGBA3forRGBA8, RGBA8forRGBA3,
    RGB3forRGBA8, RGBA8forRGB3,
    RGBA8forImageData, RGBA3forImageData, RGB3forImageData,
    imageDataForRGBA8, imageDataForRGBA3, imageDataForRGB3,
    RGB3forIDX4, RGBA3forIDX4,
};


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//
//   ####### #     # #     #                         #######
//        #   #   #  ##    #     #    #    #   ####  #     #  #####    ####
//       #     # #   # #   #     #    ##  ##  #    # #     #  #    #  #
//      #       #    #  #  #     #    # ## #  #      #     #  #    #   ####
//     #       # #   #   # #     #    #    #  #  ### #     #  #####        #
//    #       #   #  #    ##     #    #    #  #    # #     #  #       #    #
//   ####### #     # #     #     #    #    #   ####  #######  #        ####
//
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

function blit(simg, dimg, sx, sy, dx, dy, w, h)
{
    // dbglog(sx, sy, dx, dy, w, h);
    const sw = simg.width;
    const sh = simg.height;
    const dw = dimg.width;
    const dh = dimg.height;
    for (let y = 0; y < h; y++) {
        let sidx = sx + (sy + y) * sw;
        let didx = dx + (dy + y) * dw;
        for (let x = 0; x < w; x++) {
            // dbglog(didx, sidx);
            dimg.data[didx++] = simg.data[sidx++];
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// simg: source image
// tw: tile width in pixels
// th: tile height in pixels
// dnx: number of horizontal tiles in destination image
// dny: number of vertical tiles in destination image
// dnx OR dny MAY be zero:
// - dnx = 0 -> row image
// - dny = 0 -> column image

function retile(simg, tw, th, dnx, dny)
{
    // t for tile, s for source, d for destination
    const sw = simg.width;
    const sh = simg.height;
    const snx = parseInt(sw / tw)
    const sny = parseInt(sh / th)
    if (dnx == 0) dnx = Math.ceil((snx * sny) / dny);
    if (dny == 0) dny = Math.ceil((snx * sny) / dnx);
    const dw = dnx * tw;
    const dh = dny * th;

    // dbglog(`toRetiledImage: ${sw}x${sh} => ${dw}x${dh} [px] || tile ${tw}x${th} || ${snx}x${sny} => ${dnx}x${dny} [tiles]`);

    const dimg = new RawImage().create(dw, dh, simg.type);

    let di = 0;
    let dj = 0;
    for (let sj = 0; sj < sny; sj++) {
        for (let si = 0; si < snx; si++) {
            blit(simg, dimg, si*tw, sj*th, di*tw, dj*th, tw, th);
            di++;
            if (di >= dnx) {
                di = 0;
                dj++;
            }
        }
    }

    return dimg;
}

// this function compares two images and returns the number of different pixels
function compare(simg, dimg)
{
    let diffcount = Math.abs(simg.data.length - dimg.data.length);
    const checkcount = Math.min(simg.data.length , dimg.data.length);
    for (let i = 0; i < checkcount; i++) {
        if (simg.data[i] != dimg.data[i])
            diffcount++;
    }
    return diffcount;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

function extractTile(x, y, w, h)
{

}

function insertTile(tile, x, y)
{

}

// apply in-place transparncy threshold (for RGBA3 image only)
// make all pixels in image fully opaque or fully transparent.
// pixels with alpha in [0,3] will be set to r=g=b=a=0
// pixels with alpha in [4,7] will be set to a=7
function ip_applyTransparencyThreshold(img)
{
    if (img.type != 'RGBA3')
        throw new Error('Image type should be RGBA3, is ' + img.type);

    for (let i = 0; i < img.data.length; i++) {
        let sv = img.data[i];
        let r3 = sv & 0x07; sv >>= 3;
        let g3 = sv & 0x07; sv >>= 3;
        let b3 = sv & 0x07; sv >>= 3;
        let a3 = sv & 0x07; sv >>= 3;
        if (a3 >= 4) a3 = 7;
        else { r3 = 0; g3 = 0; b3 = 0; a3 = 0; }
        const dv = (r3) | (g3 << 3) | (b3 << 6) | (a3 << 9);
        img.data[i] = dv;
    }
}

function extractColorTables4(simg, tw, th)
{
    // rearrange image as column
    const cimg = retile(simg, tw, th, 1, 0);
    const tsz = tw * th;
    const tcount = parseInt(cimg.data.length / tsz);
    if(dbg)dbglog('tile count: ', tcount);

    const pvset = new Set();
    for (let i = 0; i < cimg.data.length; i++) {
        const pixval = cimg.data[i];
        pvset.add(pixval);
    }
    if(dbg)dbglog('pixel value set:', pvset);

    let haveTransparentPixel = false;
    if (pvset.has(0)) {
        haveTransparentPixel = true;
        pvset.delete(0);
    }

    const pvarr = Array.from(pvset);
    if(dbg)dbglog('pixel value array:', pvarr);

    const ctables = { fwd:{}, inv:{}};

    for (let i = 0; i < pvarr.length; i++) {
        const pixval = pvarr[i];
        ctables.fwd[i] = pixval;
        ctables.inv[pixval] = i;
    }

    ctables.pal = new RawImage().create(1, pvarr.length, 'RGB3');
    for (let i = 0; i < pvarr.length; i++) {
        const pixval = pvarr[i];
        ctables.pal.data[i] = pixval & 0x1FF;
    }

    if(dbg)dbglog('color tables: ', ctables);

    return ctables;
}

function toIndexed4(simg, tw, th, invColTable, transpindex)
{
    const cimg = retile(simg, tw, th, 1, 0);
    const tsz = tw * th;
    const tcount = parseInt(cimg.data.length / tsz);
    if(dbg)dbglog('tile count: ', tcount);

    const iimg = new RawImage().create(cimg.width, cimg.height, 'IDX4');
    let idxval;
    for (let i = 0; i < cimg.data.length; i++) {
        const pixval = cimg.data[i];
        if (pixval != 0)
            idxval = invColTable[pixval];
        else
            idxval = transpindex;
        iimg.data[i] = idxval;
    }
    return iimg;
}

function extractColorTablesGrouped4(simg, tw, th, palgroup, transpindex)
{
    // rearrange image as column
    const cimg = retile(simg, tw, th, 1, 0);
    const tsz = tw * th;
    const tcount = parseInt(cimg.data.length / tsz);
    if(dbg)dbglog('tile count: ', tcount);

    const pvsets = {};
    let palbase;
    for (let i = 0; i < cimg.data.length; i++) {
        const grpidx = parseInt(i / (tw*tw));
        palbase = palgroup.data[grpidx];
        if (!palbase) palbase = 0;
        const pixval = cimg.data[i];
        if (pixval == 0) continue; // transparent
        if (!pvsets[palbase]) pvsets[palbase] = new Set();
        pvsets[palbase].add(pixval);
    }
    if(dbg)dbglog('pixel value group sets:', pvsets);

    // let haveTransparentPixel = false;
    // if (pvset.has(0)) {
    //     haveTransparentPixel = true;
    //     pvset.delete(0);
    // }

    const ctables = { pal:null, fwd:{}, inv:{}};

    const pvarrs = {};
    for (let palbase in pvsets)
        pvarrs[palbase] = Array.from(pvsets[palbase]);
    if(dbg)dbglog('pixel value group arrays:', pvarrs);

    for (let palbase in pvarrs) {
        const pvarr = pvarrs[palbase];
        ctables.fwd[palbase] = {};
        ctables.inv[palbase] = {};
        for (let i = 0; i < pvarr.length; i++) {
            const pixval = pvarr[i];
            ctables.fwd[palbase][i] = pixval;
            ctables.inv[palbase][pixval] = i;
        }
    }

    ctables.pal = new RawImage().create(1, 256, 'RGB3');
    for (let palbase in pvarrs) {
        const pvarr = pvarrs[palbase];
        for (let i = 0; i < pvarr.length; i++) {
            const pixval = pvarr[i];
            ctables.pal.data[16*palbase+i] = pixval & 0x1FF;
        }
    }

    if(dbg)dbglog('color tables: ', ctables);

    return ctables;
}

function toIndexedGrouped4(simg, tw, th, palgroup, invColTable, transpindex)
{
    const cimg = retile(simg, tw, th, 1, 0);
    const tsz = tw * th;
    const tcount = parseInt(cimg.data.length / tsz);
    if(dbg)dbglog('tile count: ', tcount);

    const iimg = new RawImage().create(cimg.width, cimg.height, 'IDX4');
    let idxval;
    let palbase;
    for (let i = 0; i < cimg.data.length; i++) {
        const grpidx = parseInt(i / (tw*tw));
        palbase = palgroup.data[grpidx];
        if (!palbase) palbase = 0;
        const pixval = cimg.data[i];
        if (pixval != 0) {
            idxval = invColTable[palbase][pixval];
        }
        else
            idxval = transpindex;
        iimg.data[i] = idxval;
    }
    return iimg;
}



function toIndexed8(simg, invColTable, transpindex)
{
    const iimg = new RawImage().create(simg.width, simg.height, 'IDX8');
    let idxval;
    for (let i = 0; i < simg.data.length; i++) {
        const pixval = simg.data[i];
        if (pixval != 0)
            idxval = invColTable[pixval];
        else
            idxval = transpindex;
        iimg.data[i] = idxval;
    }
    return iimg;
}

function transpose(simg)
{
    const sw = simg.width;
    const sh = simg.height;
    const dw = sh;
    const dh = sw;
    const dimg = new RawImage().create(dw, dh, simg.type);
    for (let sj = 0; sj < sh; sj++) {
        for (let si = 0; si < sw; si++) {
            const sidx = si + sj * sw;
            const didx = sj + si * sh;
            dimg.data[didx] = simg.data[sidx];
        }
    }
    return dimg;
}

const ZXNimgOps = {
    blit, retile, compare,
    extractColorTables4, extractColorTablesGrouped4,
    toIndexed4, toIndexedGrouped4,
    toIndexed8, transpose,
    ip_applyTransparencyThreshold,
}

///////////////////////////////////////////////////////////////////////////////

module.exports = { RawImage, ZXNimgIO, ZXNimgConv, ZXNimgOps };

///////////////////////////////////////////////////////////////////////////////

























/*

    createFromRGBA8(rgba8image, type)
    {
        this.create(rgba8image.width, rgba8image.height, type);

        if (type == 'RGB3') {
            let sidx = 0;
            let didx = 0;
            for (let i = 0; i < this.width * this.height; i++) {
                const r = rgba8image.data[sidx++] >> 5;
                const g = rgba8image.data[sidx++] >> 5;
                const b = rgba8image.data[sidx++] >> 5;
                sidx++;
                const v = (r) | (g << 3) | (b << 6);
                this.data[didx++] = v;
            }
        }
        else if (type == 'RGBA3') {
            let sidx = 0;
            let didx = 0;
            for (let i = 0; i < this.width * this.height; i++) {
                const r = rgba8image.data[sidx++] >> 5;
                const g = rgba8image.data[sidx++] >> 5;
                const b = rgba8image.data[sidx++] >> 5;
                const a = rgba8image.data[sidx++] >> 5;
                const v = (r) | (g << 3) | (b << 6) | (a << 9);
                this.data[didx++] = v;
            }
        }

        return this;
    }

    toRGBA8(palette, palindex)
    {
        const c8f3 = (val) => ((val >> 1) | (val << 2) | (val << 5));
        const newimg = new RGBA8Image().create(this.width, this.height);
        if (this.type == 'RGB3') {
            let sidx = 0;
            let didx = 0;
            for (let i = 0; i < this.width * this.height; i++) {
                let v = this.data[sidx++];
                const r = v & 7; v >>=3;
                const g = v & 7; v >>=3;
                const b = v & 7; v >>=3;
                newimg.data[didx++] = c8f3(r);
                newimg.data[didx++] = c8f3(g);
                newimg.data[didx++] = c8f3(b);
                newimg.data[didx++] = 255;
            }
        }
        else if (this.type == 'RGBA3') {
            let sidx = 0;
            let didx = 0;
            for (let i = 0; i < this.width * this.height; i++) {
                let v = this.data[sidx++];
                const r = v & 7; v >>=3;
                const g = v & 7; v >>=3;
                const b = v & 7; v >>=3;
                const a = v & 7; v >>=3;
                newimg.data[didx++] = c8f3(r);
                newimg.data[didx++] = c8f3(g);
                newimg.data[didx++] = c8f3(b);
                newimg.data[didx++] = c8f3(a);
            }
        }
        return newimg;
    }









class RGBA8Image
{
    constructor() {
        this.width = 0;
        this.height = 0;
        this.type = 'RGBA8';
        this.data = null;
    }

    check_dimension(value, name) {
        if (typeof(value) != 'number' || isNaN(value))
            throw new Error(name + ' is not a number');
        if (value < 1 || value > 65536)
            throw new Error(name + ' = ' + value + ' out of range [1,63356]');
    }

    create(width, height)
    {
        this.check_dimension(width, 'width');
        this.check_dimension(height, 'height');

        this.width = width;
        this.height = height;

        this.canvas = document.createElement('canvas');
        this.canvas.width = width;
        this.canvas.height = height;
        this.ctx = this.canvas.getContext('2d');
        this.imgdata = this.ctx.createImageData(this.canvas.width, this.canvas.height);
        this.data = this.imgdata.data;

        return this;
    }

    createCheckered(width, height, checkerSize)
    {
        this.create(width, height, 'RGBA8');

        let idx = 0;
        for (let j = 0; j < height; j++) {
            const ej = parseInt(j / checkerSize) & 1;
            for (let i = 0; i < width; i++) {
                const ei = parseInt(i / checkerSize) & 1;
                const val = (ei ^ ej) ? 0xAA : 0x55;
                this.data[idx++] = val;
                this.data[idx++] = val;
                this.data[idx++] = val;
                this.data[idx++] = 0xFF;
            }
        }

        return this;
    }

    async loadImageFile(filename)
    {
        const ext = PathUtil.extension_for_filepath(filename);
        const b64 = await FileUtils.readFileAsBase64(filename);
        if (!b64) throw new Error('cannot load file ' + filename + ' as base64');
        const dataURL = dataURLprefix.replace('EXT', ext) + b64;
        const image = await this.imageFromDataURL(dataURL);
        this.create(image.width, image.height, 'RGBA8');
        this.ctx.drawImage(image, 0, 0);
        this.imgdata = this.ctx.getImageData(0, 0, image.width, image.height);
        this.data = this.imgdata.data;

        return this;
    }

    toDataURL()
    {
        this.ctx.putImageData(this.imgdata, 0, 0);
        return this.canvas.toDataURL();
    }

    async imageFromDataURL(dataURL)
    {
        return new Promise((resolve, reject) => {
            const image = new Image();
            image.src = dataURL;
            image.onload = () => resolve(image);
            image.onerror = () => reject();
        });
    }

    async toImage()
    {
        return this.imageFromDataURL(this.toDataURL());
    }
}




    // tw: tile width in pixels
    // th: tile height in pixels
    // dnx: number of horizontal tiles in destination image
    // dny: number of vertical tiles in destination image
    // dnx OR dny MAY be zero:
    // - dnx = 0 -> row image
    // - dny = 0 -> column image
    toRetiledImage(tw, th, dnx, dny)
    {
        // t for tile, s for source, d for destination
        const sw = this.width;
        const sh = this.height;
        const snx = parseInt(sw / tw)
        const sny = parseInt(sh / th)
        if (dnx == 0) dnx = Math.ceil((snx * sny) / dny);
        if (dny == 0) dny = Math.ceil((snx * sny) / dnx);
        const dw = dnx * tw;
        const dh = dny * th;

        dbglog(`toRetiledImage: ${sw}x${sh} => ${dw}x${dh} [px] || tile ${tw}x${th} || ${snx}x${sny} => ${dnx}x${dny} [tiles]`);

        const dimg = new RawImage().create(dw, dh, this.type);

        let di = 0;
        let dj = 0;
        for (let sj = 0; sj < sny; sj++) {
            for (let si = 0; si < snx; si++) {
                blit(this, dimg, si*tw, sj*th, di*tw, dj*th, tw, th);
                di++;
                if (di >= dnx) {
                    di = 0;
                    dj++;
                }
            }
        }

        return dimg;
    }




*/



