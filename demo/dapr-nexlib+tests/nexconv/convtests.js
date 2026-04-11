const { dbgwarn, dbglog, dbgerr } = require("./common/dbglog.js");
const { ImageIO, ImageUtil, ImageDataCreator } = require('./ImageUtil.js');
const { RawImage, ZXNimgConv, ZXNimgIO, ZXNimgOps } = require('./ZXNimage');

const tests = [
{desc : 'generate misc images', func : async function () {
    let imgdata;
    imgdata = ImageDataCreator.empty(128, 128);
    await ImageIO.writeFileFromImageData(imgdata, './convtests/test0empty.png');

    imgdata = ImageDataCreator.black(128, 128);
    await ImageIO.writeFileFromImageData(imgdata, './convtests/test0black.png');

    imgdata = ImageDataCreator.random(128, 128);
    await ImageIO.writeFileFromImageData(imgdata, './convtests/test0random.png');
    
    imgdata = ImageDataCreator.checkers(128, 128, 32, 32);
    await ImageIO.writeFileFromImageData(imgdata, './convtests/test0checkers.png');
    
    imgdata = ImageDataCreator.gradient256();
    await ImageIO.writeFileFromImageData(imgdata, './convtests/test0gradient.png');
}},
{desc : 'save as RGB3', func : async function () {
    let imgdata;
    imgdata = ImageDataCreator.gradient256();
    // imgdata = ImageDataCreator.random(128, 128);
    imgC3 = ZXNimgConv.RGBA3forImageData(imgdata);
    imgC3.write_png('./convtests/test1gradient.png');
}},
{desc : 'TBD', func : async function () {

}},
]

async function run_tests()
{
    if (process.argv.length < 3) {
        dbglog('USAGE: convtests TEST_NUMBER');
        dbglog('where TEST_NUMBER is one of:');
        for (let i in tests) {
            dbglog(i + ' : ' + tests[i].desc);
        }
    }
    else {
        const idx = process.argv[2];
        if (!tests[idx]) {
            dbgwarn('bad TEST_NUMBER - should be in the range 0 to ' + (tests.length - 1));
        }
        else {
            tests[idx].func()
        }
    }

}

async function test01()
{

}

run_tests();
