const { dbglog, dbgwarn, dbgerr } = require("../../nexconv/common/dbglog");
const { conv_layer2, conv_tileset, conv_sprites } = require('../../nexconv/nexconv.js');

async function run()
{
    await conv_layer2('./kanagawa-zxn-layer2.png', './kanagawa');
}

run();
