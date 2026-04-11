const { run_tilemapper } = require('../../nexconv/tilemapper.js');

async function run()
{
    await run_tilemapper('screen01.png', 'screen01');
}

run()