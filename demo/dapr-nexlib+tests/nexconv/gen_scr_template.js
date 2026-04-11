const { dbgwarn, dbglog, dbgerr } = require("./common/dbglog.js");
const FileUtils = require('./common/FileUtils');

const h_templ = `#ifndef _FILENAME_
#define _FILENAME_

void PREFIX_init(void);
void PREFIX_update(void);

#endif

`;

const c_templ = `#include "FILENAME.h"

void PREFIX_init(void)
{

}

void PREFIX_update(void)
{

}

`;

FileUtils.disableLogging();

async function run()
{
    if (process.argv.length < 4) {
        dbglog('USAGE: gen_scr_template FILENAME PREFIX');
        dbglog('example: FILENAME=scr_main PREFIX=sm');
    }
    else {
        const FILENAME = process.argv[2];
        const PREFIX   = process.argv[3];
        dbglog("generating screen template");
        dbglog("FILENAME=" + FILENAME + ", PREFIX=" + PREFIX)

        const h_fn = FILENAME + '.h';
        const c_fn = FILENAME + '.c';

        if (await FileUtils.readFullTextFile(h_fn)) {
            dbglog('cowardly refusing to overwrite file ' + h_fn);
        }
        else {
            const h_txt = h_templ
                .replaceAll("FILENAME", FILENAME)
                .replaceAll("PREFIX", PREFIX);

            await FileUtils.writeFullTextFile(h_fn, h_txt);
            dbglog('generated file ' + h_fn);
        }

        if (await FileUtils.readFullTextFile(c_fn)) {
            dbglog('cowardly refusing to overwrite file ' + c_fn);
        }
        else {
            const c_txt = c_templ
                .replaceAll("FILENAME", FILENAME)
                .replaceAll("PREFIX", PREFIX);
            await FileUtils.writeFullTextFile(c_fn, c_txt);
            dbglog('generated file ' + c_fn);
        }
    }
}

run();
