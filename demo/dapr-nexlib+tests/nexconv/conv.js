const fs = require("fs");
const PNG = require("pngjs").PNG;

const { RawImage, ZXNimgConv, ZXNimgIO, ZXNimgOps } = require('./ZXNimage.js');
const { dbgwarn, dbglog } = require("./common/dbglog.js");
const { ImageIO } = require("./ImageUtil.js");
const { exec } = require('child_process');
const { PathUtil } = require("./common/PathUtil.js");
const { readFullTextFile, readFullBinaryFile, writeFullTextFile } = require("./common/FileUtils.js");

const FileUtils = require('./common/FileUtils');
// const FileUtils = electronAPI;   // for running on Electron renderer

function test_IO() {
    fs.createReadStream("test/in.png")
        .pipe(
            new PNG({
                filterType: 4,
            })
        )
        .on("parsed", function () {
            for (let y = 0; y < this.height; y++) {
                for (let x = 0; x < this.width; x++) {
                    const idx = (this.width * y + x) << 2;

                    // invert color
                    this.data[idx] = 255 - this.data[idx];
                    this.data[idx + 1] = 255 - this.data[idx + 1];
                    this.data[idx + 2] = 255 - this.data[idx + 2];

                    // and reduce opacity
                    this.data[idx + 3] = this.data[idx + 3] >> 1;
                }
            }

            this.pack().pipe(fs.createWriteStream("test/out.png"));
        });
}

test_IO();


// ZXNimgIO.loadTextPalgroup("../../art/jungle0/jungle0-sprites.spalgrp")

async function exec_zx0(filename)
{
    return new Promise((resolve, reject) => {
        try {
            exec('./zx0.exe -f ' + filename, (err, stdout, stderr) => {
                if (err) {
                    // node couldn't execute the command
                    dbgwarn(err);
                    return;
                }
            
                // the *entire* stdout and stderr (buffered)
                if (stdout) console.log(`stdout: ${stdout}`);
                if (stderr) console.log(`stderr: ${stderr}`);
                resolve();
            });
        }
        catch(err) {
            reject(err);
        }
    });
}

async function exec_delete(filename)
{
    return new Promise((resolve, reject) => {
        try {
            exec('rm -f ' + filename, (err, stdout, stderr) => {
                if (err) {
                    // node couldn't execute the command
                    dbgwarn(err);
                    return;
                }
            
                // the *entire* stdout and stderr (buffered)
                if (stdout) console.log(`stdout: ${stdout}`);
                if (stderr) console.log(`stderr: ${stderr}`);
                resolve();
            });
        }
        catch(err) {
            reject(err);
        }
    });
}


let resroot;
let spath, dpath, lpath, rpath;
let screentable = {};
let levellist = [];
let reslist = [];
let filelist = [];
let pagelist = [];

async function sprites_import(inbase, outbase)
{
    const resource = { type:'sp' };

    const palgroup = await ZXNimgIO.loadTextPalgroup(spath + inbase + '.spalgrp');
    // dbgwarn(palgroup);

    const ori8img = await ImageIO.imageDataFromFile(spath + inbase + '.png');
    const ori3img = ZXNimgConv.RGBA3forImageData(ori8img);
    ZXNimgOps.ip_applyTransparencyThreshold(ori3img);

    const ori3img2 = ZXNimgOps.retile(ori3img, 32, 32, 1, 0);
    // const ctables = ZXNimgOps.extractColorTables4(ori3img2, 16, 16);
    const ctables = ZXNimgOps.extractColorTablesGrouped4(ori3img2, 16, 16, palgroup, 15);

    const palimage = ZXNimgOps.retile(ctables.pal, 1, 1, 16, 0);

    const idx4img = ZXNimgOps.toIndexedGrouped4(ori3img2, 16, 16, palgroup, ctables.inv, 15);
    // dbglog('idx4img:', idx4img);

    const rec3img = ZXNimgConv.RGBA3forIDX4(idx4img, ctables.pal, palgroup, 15);
    const rec3img2 = ZXNimgOps.retile(rec3img, 16, 16, 16, 0);

    const blocks8k = idx4img.split_into_8kb_blocks();
    const letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (let i = 0; i < blocks8k.length; i++) {
        const block8k = blocks8k[i];
        const letter = letters[i];
        let filename = dpath + outbase + '_sp_' + letter 
        await ZXNimgIO.saveBinTilesetFile(block8k, filename);
        await exec_zx0(filename);
        await exec_delete(filename);
        filename += '.zx0';
        const key = 'sp_' + letter;
        resource[key] = { fn:filename, sz:await getFileLength(filename) };
    }

    await ZXNimgIO.saveBinPaletteFile(ctables.pal, dpath + outbase + '_sp_pal');

    let fn_pal = dpath + outbase + '_sp_pal'
    await ZXNimgIO.saveBinPaletteFile(ctables.pal, fn_pal);
    const colcount = (await getFileLength(fn_pal) / 2) | 0;
    await exec_zx0(fn_pal);
    await exec_delete(fn_pal);
    fn_pal += '.zx0';
    const key = 'sp_pal';
    resource[key] = { fn:fn_pal, sz:await getFileLength(fn_pal), colcount };

    return resource;
}

async function layer2_import(inbase, outbase)
{
    const resource = { type:'l2' };

    const ori8img = await ImageIO.imageDataFromFile(spath + inbase + '.png');
    const ori3img = ZXNimgConv.RGBA3forImageData(ori8img);
    ZXNimgOps.ip_applyTransparencyThreshold(ori3img);

    const tra3img = ZXNimgOps.transpose(ori3img);

    const ctables = ZXNimgOps.extractColorTables4(tra3img, 16, 16, null, 15);
    const palimage = ZXNimgOps.retile(ctables.pal, 1, 1, 16, 0);

    const idx8img = ZXNimgOps.toIndexed8(tra3img, ctables.inv, 255);

    let idx = 0;
    let start = 0;
    let size = 64;
    let remain = idx8img.height;
    while (remain > 0) {
        if (size > remain)
            size = remain;
        const strip = new RawImage().create(idx8img.width, size, 'IDX8');
        ZXNimgOps.blit(idx8img, strip, 0, start, 0, 0, idx8img.width, size);
        remain -= size;
        start += size;

        const blocks8k = strip.split_into_8kb_blocks();
        const letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        for (let i = 0; i < blocks8k.length; i++) {
            const block8k = blocks8k[i];
            const letter = letters[i];
            let fn_l2 = dpath + outbase + '_l2_' + idx + letter;
            await ZXNimgIO.saveBinLayer2(block8k, fn_l2);
            await exec_zx0(fn_l2);
            await exec_delete(fn_l2);
            fn_l2 += '.zx0';
            const key = 'l2_' + idx + letter;
            resource[key] = { fn:fn_l2, sz:await getFileLength(fn_l2) };
        }

        idx++;
    }

    let fn_pal = dpath + outbase + '_l2_pal'
    await ZXNimgIO.saveBinPaletteFile(ctables.pal, fn_pal);
    const colcount = (await getFileLength(fn_pal) / 2) | 0;
    await exec_zx0(fn_pal);
    await exec_delete(fn_pal);
    fn_pal += '.zx0';
    const key = 'l2_pal';
    resource[key] = { fn:fn_pal, sz:await getFileLength(fn_pal), colcount };

    return resource;
}

async function layer2_import_raw(inbase, outbase)
{
    const resource = { type:'l2' };

    const raw_img = new RawImage().create(320, 256, 'IDX8');
    raw_img.data = await FileUtils.readFullBinaryFile(rpath + inbase);

    const idx8img = ZXNimgOps.transpose(raw_img);

    let idx = 0;
    let start = 0;
    let size = 64;
    let remain = idx8img.height;
    while (remain > 0) {
        if (size > remain)
            size = remain;
        const strip = new RawImage().create(idx8img.width, size, 'IDX8');
        ZXNimgOps.blit(idx8img, strip, 0, start, 0, 0, idx8img.width, size);
        remain -= size;
        start += size;

        const blocks8k = strip.split_into_8kb_blocks();
        const letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        for (let i = 0; i < blocks8k.length; i++) {
            const block8k = blocks8k[i];
            const letter = letters[i];
            let fn_l2 = dpath + outbase + '_l2_' + idx + letter;
            await ZXNimgIO.saveBinLayer2(block8k, fn_l2);
            await exec_zx0(fn_l2);
            await exec_delete(fn_l2);
            fn_l2 += '.zx0';
            const key = 'l2_' + idx + letter;
            resource[key] = { fn:fn_l2, sz:await getFileLength(fn_l2) };
        }

        idx++;
    }

    const paldata = await FileUtils.readFullBinaryFile(rpath + inbase + '_pal');

    let fn_pal = dpath + outbase + '_l2_pal'
    await FileUtils.writeFullBinaryFile(fn_pal, paldata);
    const colcount = (await getFileLength(fn_pal) / 2) | 0;
    await exec_zx0(fn_pal);
    await exec_delete(fn_pal);
    fn_pal += '.zx0';
    const key = 'l2_pal';
    resource[key] = { fn:fn_pal, sz:await getFileLength(fn_pal), colcount };

    return resource;
}

async function tileset_import(inbase, outbase)
{
    let palgroup = null;
    try { palgroup = await ZXNimgIO.loadTextPalgroup(spath + inbase + '.tpalgrp'); }
    catch(err) { dbglog("no palgroup available for " + inbase) };

    const ori8img = await ImageIO.imageDataFromFile(spath + inbase + '.png');
    const ori3img = ZXNimgConv.RGBA3forImageData(ori8img);
    ZXNimgOps.ip_applyTransparencyThreshold(ori3img);

    let ctables;
    if (!palgroup) ctables = ZXNimgOps.extractColorTables4(ori3img, 8, 8);
    else           ctables = ZXNimgOps.extractColorTablesGrouped4(ori3img, 8, 8, palgroup, 15)

    const palimage = ZXNimgOps.retile(ctables.pal, 1, 1, 16, 0);

    let idx4img;
    if (!palgroup) idx4img = ZXNimgOps.toIndexed4(ori3img, 8, 8, ctables.inv, 15);
    else           idx4img = ZXNimgOps.toIndexedGrouped4(ori3img, 8, 8, palgroup, ctables.inv, 15);

    const rec3img = ZXNimgConv.RGBA3forIDX4(idx4img, ctables.pal, null, 15);

    const rec3img2 = ZXNimgOps.retile(rec3img, 8, 8, 16, 0);

    let fn_ts = dpath + outbase + '_ts'
    await ZXNimgIO.saveBinTilesetFile(idx4img, fn_ts);
    await exec_zx0(fn_ts);
    await exec_delete(fn_ts);
    fn_ts += '.zx0';

    const resource = { type:"ts", ts: { fn:fn_ts, sz:await getFileLength(fn_ts)} };

    let fn_pal = dpath + outbase + '_ts_pal'
    await ZXNimgIO.saveBinPaletteFile(ctables.pal, fn_pal);
    const colcount = (await getFileLength(fn_pal) / 2) | 0;
    await exec_zx0(fn_pal);
    await exec_delete(fn_pal);
    fn_pal += '.zx0';
    const key = 'ts_pal';
    resource[key] = { fn:fn_pal, sz:await getFileLength(fn_pal), colcount };

    if (palgroup) {
        for (let i = 0; i < palgroup.data.length; i++)
            palgroup.data[i] <<= 4;
        let fn_palgrp = dpath + outbase + '_ts_palgrp';
        await ZXNimgIO.saveBinPalgroup(palgroup, fn_palgrp);
        await exec_zx0(fn_palgrp);
        await exec_delete(fn_palgrp);
        fn_palgrp += '.zx0';
        const key = 'ts_palgrp';
        resource[key] = { fn:fn_palgrp, sz:await getFileLength(fn_palgrp) };
    }

    return (resource);
}

async function getFileLength(filename) {
    const rawdata = await readFullBinaryFile(filename);
    return rawdata.length;
}

async function loadAndSplit(filename)
{
    const img = new RawImage().create(1, 1, "IDX8");
    img.data = await FileUtils.readFullBinaryFile(filename);
    await exec_zx0(filename);

    const dir = PathUtil.dirpart_for_filepath(filename);
    const bas = PathUtil.basename_for_filepath(filename);
    const ext = PathUtil.extension_for_filepath(filename);
    dbglog(dir, bas, ext);

    const blocks8k = img.split_into_8kb_blocks();
    const letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (let i = 0; i < blocks8k.length; i++) {
        const block8k = blocks8k[i];
        const letter = letters[i];
        const filename8k = dir + '/' + bas + '.8k' + letter + '.' + ext;
        await ZXNimgIO.saveBinLayer2(block8k, filename8k);
        await exec_zx0(filename8k);
    }
}

async function loadAndSplitLevelTmCm(id, type)
{
    const resource = { type:type };

    const img = new RawImage().create(1, 1, "IDX8");
    const basename = id + '_' + type;
    img.data = await FileUtils.readFullBinaryFile(lpath + basename);

    const blocks8k = img.split_into_8kb_blocks();
    const letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (let i = 0; i < blocks8k.length; i++) {
        const block8k = blocks8k[i];
        const letter = letters[i];
        let filename = dpath + basename + '_' + letter;
        await ZXNimgIO.saveBinLayer2(block8k, filename);
        await exec_zx0(filename);
        await exec_delete(filename);
        filename += '.zx0';
        resource[type + '_' + letter] = {
            fn : filename,
            sz : await getFileLength(filename)
        };
    }
    return resource;
}

async function loadAndCompressLevelData(id, type)
{
    const resource = { type:type };

    const img = new RawImage().create(1, 1, "IDX8");
    const basename = id + '_' + type;
    const data = await FileUtils.readFullBinaryFile(lpath + basename);

    let filename = dpath + basename;
    await FileUtils.writeFullBinaryFile(filename, data);
    await exec_zx0(filename);
    await exec_delete(filename);
    filename += '.zx0';
    resource[type] = {
        fn : filename,
        sz : await getFileLength(filename)
    };
    return resource;
}

async function process_res_list_entry(entry)
{
    if (entry.id in screentable) throw new Error("duplicate id " + entry.id);
    screentable[entry.id] = {};

    if (entry.is_level) {
        let resource;
        screentable[entry.id]['tm'] = reslist.length;
        resource = await loadAndSplitLevelTmCm(entry.id, 'tm');
        reslist.push(resource);
        screentable[entry.id]['cm'] = reslist.length;
        resource = await loadAndSplitLevelTmCm(entry.id, 'cm');
        reslist.push(resource);
        screentable[entry.id]['ld'] = reslist.length;
        resource = await loadAndCompressLevelData(entry.id, 'ld');
        reslist.push(resource);
    }
    if (entry.l2) {
        screentable[entry.id]['l2'] = reslist.length;
        const resource = await layer2_import(entry.l2, entry.id);
        reslist.push(resource);
    }
    if (entry.raw_l2) {
        screentable[entry.id]['l2'] = reslist.length;
        const resource = await layer2_import_raw(entry.raw_l2, entry.id);
        reslist.push(resource);
    }
    if (entry.sp) {
        screentable[entry.id]['sp'] = reslist.length;
        const resource = await sprites_import(entry.sp, entry.id);
        reslist.push(resource);
    }
    if (entry.ts) {
        screentable[entry.id]['ts'] = reslist.length;
        const resource = await tileset_import(entry.ts, entry.id)
        reslist.push(resource);
    }
    if (entry.font) {
        screentable[entry.id]['font'] = reslist.length;
        const resource = await tileset_import(entry.font.src, entry.font.dst);
        reslist.push(resource);
    }
}

async function process_res_list_entry_refs(entry)
{
    if(entry.ref_l2) {
        screentable[entry.id]['l2'] = screentable[entry.ref_l2]['l2'];
    }
    if(entry.ref_sp) {
        screentable[entry.id]['sp'] = screentable[entry.ref_sp]['sp'];
    }
    if(entry.ref_ts) {
        screentable[entry.id]['ts'] = screentable[entry.ref_ts]['ts'];
    }
    if(entry.ref_font) {
        screentable[entry.id]['font'] = screentable[entry.ref_font]['font'];
    }
}

async function run_old()
{
    spath = '../../art/jungle0/';
    dpath = '../res/';

    await tileset_import("jungle_ts.png", "jungle0");
    await tileset_import("font0.png", "font");
    await layer2_import("jungle_bg.png", "jungle_bg");
    await sprites_import("jungle0-sprites", "jungle0");
    loadAndSplit("../res/level_tiledata_jungle0.bin");
    loadAndSplit("../res/level_colldata_jungle0.bin");

    spath = '../../art/mainmenu/';
    await layer2_import("main.png", "main");
}

function analyze_resources()
{
    for (let resource of reslist) {
        const type = resource.type;
        for (let key in resource) {
            if (key.startsWith(type)) {
                filelist.push(resource[key]);
            }
        }
    }
    // dbglog(filelist);
}

function pack_resources()
{
    let worklist = [];
    for (let file of filelist)
        worklist.push(file);
    worklist.sort((a,b)=>(b.sz-a.sz));
    dbglog(worklist);

    let totalsize = 0;
    for (let file of worklist)
        totalsize += file.sz;
    dbglog('total size of files: ', totalsize);

    const pagecount = Math.ceil(totalsize / 8192);
    dbglog('number of (theoretical required) pages: ', pagecount);

    for (let i = 0; i < pagecount; i++)
        pagelist[i] = { remain: 8192, filelist: [] };

    let pageidx = 0;

    let prev_file_count = worklist.length;
    for (let j = 0; j < 10; j++) {
        const file_indices_to_remove = new Set();
        const prev_pageidx = pageidx;
        for (let i = 0; i < worklist.length; i++) {
            const file = worklist[i];
            do {
                const page = pagelist[pageidx];
                if (page.remain >= file.sz) {
                    page.remain -= file.sz;
                    page.filelist.push(file);
                    file.pg = pageidx;
                    file_indices_to_remove.add(i);
                    pageidx++;
                    if (pageidx >= pagecount)
                        pageidx = 0;
                        break;
                }
                pageidx++;
                if (pageidx >= pagecount)
                    pageidx = 0;
            }
            while (pageidx != prev_pageidx);
        }

        const newlist = [];
        for (let i = 0; i < worklist.length; i++) {
            if (!file_indices_to_remove.has(i))
                newlist.push(worklist[i]);
        }
        worklist = newlist;

        if (worklist.length == 0)
            break;
    }

    // dbglog(worklist);

    if (worklist.length > 0) {
        dbgwarn('extra page needed - could not fit everything');
        pageidx = pagelist.length;
        const extrapage = { remain: 8192, filelist: [] };
        for (let file of worklist) {
            extrapage.remain -= file.sz;
            extrapage.filelist.push(file);
            file.pg = pageidx;
        }
        pagelist.push(extrapage);
        if (extrapage.remain < 0) throw new Error("algorighm must be hardened");
    }

    for (let page of pagelist) {
        let addr = 0;
        for (let file of page.filelist) {
            file.addr = addr;
            addr += file.sz;
        }
    }
}

async function write_resource_pages()
{
    for (let i = 0; i < pagelist.length; i++)
    {
        const pgnum = resroot.res_start_page + i;

        let txt = ''
        const line = (s) => { txt += s; txt += '\n' }

        line(`SECTION PAGE_${pgnum}_RESOURCES\n`);
        line("; This file has been generated by a tool.");
        line("; DO NOT EDIT unless you know what you're doing.\n");
        line('ORG $0000\n');

        const filelist = pagelist[i].filelist;
        for (let file of filelist) {
            const fn = file.fn.replace('../', '');
            const symbol = fn.replace('res/', '').replace('.zx0', '');
            line(`; START ${hexASM(file.addr)} | LEN ${hexASM(file.sz)} (${file.sz})`);
            line(`PUBLIC _${symbol}`);
            line(`_${symbol}:`);
            line(`    INCBIN "${fn}"\n`);
        }

        const rem = pagelist[i].remain;
        line(`; remaining bytes in 8k page: ${hexASM(rem)} (${rem})`);

        await writeFullTextFile(resroot.dbg_path + `pg${pgnum}.asm`, txt)
        await writeFullTextFile(resroot.out_path + `pg${pgnum}.asm`, txt)
    }
}

function rpad(first, pad, second)
{
    while (first.length < pad) first += ' '
    return first + second;
}

async function write_screen_list()
{
    let txt = '';
    const line = (s) => { if (s) txt += s; txt += '\n' }

    const pgnum = resroot.scr_list_page;

    // line(`SECTION PAGE_${pgnum}_SCREENLIST\n);
    // line("; This file has been generated by a tool.\n"
    // line("; DO NOT EDIT unless you know what you're doing.\n\n"
    // line('ORG $0000\n\n'

    line('SECTION code_user\n');
    line("; This file has been generated by a tool.");
    line("; DO NOT EDIT unless you know what you're doing.\n");

    line("PUBLIC _screen_list");
    line("_screen_list:\n");

    for (let i = 0; i < resroot.list.length; i++)
    {
        const id = resroot.list[i].id;
        const scrdef = screentable[id];

        line(';;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;');
        line(`; SCREEN #${i} id = ${id}`);
        line(';;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n');

        line("PUBLIC _screen_" + i);
        line("_screen_" + i + ":\n");
        line("PUBLIC _screen_" + id);
        line("_screen_" + id + ":\n");
    
        line('; layer2: 10 BlockRes blocks (30 bytes)');

        if ('l2' in scrdef) {
            const res = reslist[scrdef.l2];
            for (let index of "01234") {
                for (let letter of "AB") {
                    const blkname = `layer2_${index}${letter}`
                    const key = `l2_${index}${letter}`
                    const block = res[key];
                    const pg = resroot.res_start_page + block.pg;
                    line(rpad('DEFB ' + pg,         15, ' ; ' + blkname + ' page'));
                    line(rpad('DEFW ' + block.addr, 15, ' ; ' + blkname + ' addr'));
                }
            }
        }
        else {
            line(rpad('DEFS 30, 0', 15, ' ; NOT PRESENT'));
        }
        line();

        line('; layer2 palette: 1 PalRes block (5 bytes)');

        if ('l2' in scrdef) {
            const res = reslist[scrdef.l2];
            const blkname = "layer2_pal";
            const key = "l2_pal";
            const block = res[key];
            const pg = resroot.res_start_page + block.pg;
            line(rpad('DEFB ' + pg,         15, ' ; ' + blkname + ' page'));
            line(rpad('DEFW ' + block.addr, 15, ' ; ' + blkname + ' addr'));
            line(rpad('DEFB ' + block.colcount, 15, ' ; ' + blkname + ' count'));
            line(rpad('DEFB ' + 0, 15, ' ; ' + blkname + ' has_fase'));
        }
        else {
            line(rpad('DEFS 5, 0', 15, ' ; NOT PRESENT'));
        }
        line();

        line('; main tileset: 1 BlockRes block (3 bytes)');
        if ('ts' in scrdef) {
            const res = reslist[scrdef.ts];
            const block = res['ts'];
            const pg = resroot.res_start_page + block.pg;
            line(rpad('DEFB ' + pg,         15, ' ; tileset_main page'));
            line(rpad('DEFW ' + block.addr, 15, ' ; tileset_main addr'));
        }
        else {
            line(rpad('DEFS 3, 0', 15, ' ; NOT PRESENT'));
        }
        line();

        line('; main tileset palette: 1 PalRes blocks (5 bytes)');
        if ('ts' in scrdef) {
            const res = reslist[scrdef.ts];
            const block = res['ts_pal'];
            const pg = resroot.res_start_page + block.pg;
            line(rpad('DEFB ' + pg,         15, ' ; tileset_main_pal page'));
            line(rpad('DEFW ' + block.addr, 15, ' ; tileset_main_pal addr'));
            line(rpad('DEFB ' + block.colcount, 15, ' ; tileset_main_pal count'));
            line(rpad('DEFB ' + 0, 15, ' ; tileset_main_pal has_fase'));
        }
        else {
            line(rpad('DEFS 5, 0', 15, ' ; NOT PRESENT'));
        }
        line();

        line('; main tileset palette group: 1 BlockRes block (3 bytes)');
        if ('ts' in scrdef) {
            const res = reslist[scrdef.ts];
            const block = res['ts_palgrp'];
            if (!block) { dbglog('on resource', res); dbglog('... missing palgrp!!!') }
            const pg = resroot.res_start_page + block.pg;
            line(rpad('DEFB ' + pg,         15, ' ; tileset_main_pal_group page'));
            line(rpad('DEFW ' + block.addr, 15, ' ; tileset_main_pal_group addr'));
        }
        else {
            line(rpad('DEFS 3, 0', 15, ' ; NOT PRESENT'));
        }
        line();

        line('; text tileset: 1 BlockRes block (3 bytes)');
        if ('font' in scrdef) {
            const res = reslist[scrdef.font];
            const block = res['ts'];
            const pg = resroot.res_start_page + block.pg;
            line(rpad('DEFB ' + pg,         15, ' ; tileset_text page'));
            line(rpad('DEFW ' + block.addr, 15, ' ; tileset_text addr'));
        }
        else {
            line(rpad('DEFS 3, 0', 15, ' ; NOT PRESENT'));
        }
        line();

        line('; text tileset palette: 1 PalRes blocks (5 bytes)');
        if ('font' in scrdef) {
            const res = reslist[scrdef.font];
            const block = res['ts_pal'];
            const pg = resroot.res_start_page + block.pg;
            line(rpad('DEFB ' + pg,         15, ' ; tileset_text_pal page'));
            line(rpad('DEFW ' + block.addr, 15, ' ; tileset_text_pal addr'));
            line(rpad('DEFB ' + block.colcount, 15, ' ; tileset_text_pal count'));
            line(rpad('DEFB ' + 0, 15, ' ; tileset_text_pal has_fase'));
        }
        else {
            line(rpad('DEFS 5, 0', 15, ' ; NOT PRESENT'));
        }
        line();

        line('; spriteset: 2 BlockRes blocks (6 bytes)');

        if ('sp' in scrdef) {
            const residx = scrdef.sp;
            const res = reslist[residx];
            for (let letter of "AB") {
                const blkname = `spriteset_${letter}`
                const key = `sp_${letter}`
                const block = res[key];
                const pg = resroot.res_start_page + block.pg;
                line(rpad('DEFB ' + pg,         15, ' ; ' + blkname + ' page'));
                line(rpad('DEFW ' + block.addr, 15, ' ; ' + blkname + ' addr'));
            }
        }
        else {
            line(rpad('DEFS 6, 0', 15, ' ; NOT PRESENT'));
        }
        line();

        line('; spriteset: 1 PalRes block (5 bytes)');
        if ('sp' in scrdef) {
            const res = reslist[scrdef.sp];
            const block = res['sp_pal'];
            const pg = resroot.res_start_page + block.pg;
            line(rpad('DEFB ' + pg,         15, ' ; spriteset_pal page'));
            line(rpad('DEFW ' + block.addr, 15, ' ; spriteset_pal addr'));
            line(rpad('DEFB ' + block.colcount, 15, ' ; spriteset_pal count'));
            line(rpad('DEFB ' + 0, 15, ' ; spriteset_pal has_fase'));
        }
        else {
            line(rpad('DEFS 5, 0', 15, ' ; NOT PRESENT'));
        }
        line();

        line('; tilemap: 2 BlockRes blocks (6 bytes)');

        if ('tm' in scrdef) {
            const residx = scrdef.tm;
            const res = reslist[residx];
            for (let letter of "AB") {
                const blkname = `tilemap_${letter}`
                const key = `tm_${letter}`
                const block = res[key];
                const pg = resroot.res_start_page + block.pg;
                line(rpad('DEFB ' + pg,         15, ' ; ' + blkname + ' page'));
                line(rpad('DEFW ' + block.addr, 15, ' ; ' + blkname + ' addr'));
            }
        }
        else {
            line(rpad('DEFS 6, 0', 15, ' ; NOT PRESENT'));
        }
        line();

        line('; collmap: 2 BlockRes blocks (6 bytes)');

        if ('cm' in scrdef) {
            const residx = scrdef.cm;
            const res = reslist[residx];
            for (let letter of "AB") {
                const blkname = `collmap_${letter}`
                const key = `cm_${letter}`
                const block = res[key];
                const pg = resroot.res_start_page + block.pg;
                line(rpad('DEFB ' + pg,         15, ' ; ' + blkname + ' page'));
                line(rpad('DEFW ' + block.addr, 15, ' ; ' + blkname + ' addr'));
            }
        }
        else {
            line(rpad('DEFS 6, 0', 15, ' ; NOT PRESENT'));
        }
        line();

        line('; levdata: 1 BlockRes block(3 bytes)')

        if ('ld' in scrdef) {
            const residx = scrdef.ld;
            const res = reslist[residx];
            const blkname = `levdata`
            const key = `ld`
            const block = res[key];
            const pg = resroot.res_start_page + block.pg;
            line(rpad('DEFB ' + pg,         15, ' ; ' + blkname + ' page'));
            line(rpad('DEFW ' + block.addr, 15, ' ; ' + blkname + ' addr'));
        }
        else {
            line(rpad('DEFS 3, 0', 15, ' ; NOT PRESENT'));
        }
        line();

    }

    // await writeFullTextFile(resroot.dbg_path + `pg${pgnum}.asm`, txt)
    await writeFullTextFile(resroot.dbg_path + `screen_list.asm`, txt)
    await writeFullTextFile(resroot.out_path + `screen_list.asm`, txt)
}

function hexASM(val)
{
    return '$' + val.toString(16).toUpperCase();
}


async function run()
{
    resroot = JSON.parse(await readFullTextFile("./resource_list.json"));

    const skipFileGeneration = false;
    if (!skipFileGeneration) {
        spath = resroot.src_path;
        rpath = resroot.raw_path;
        dpath = resroot.dst_path;
        lpath = resroot.lev_path;

        for (let entry of resroot.list)
            await process_res_list_entry(entry);
        for (let entry of resroot.list)
            await process_res_list_entry_refs(entry);


        // dbglog(reslist);
        // dbglog(screentable);
        await writeFullTextFile(resroot.dbg_path + "reslist.json", JSON.stringify(reslist, null, 4));
        await writeFullTextFile(resroot.dbg_path + "screentable.json", JSON.stringify(screentable, null, 4));
        await writeFullTextFile(resroot.dbg_path + "filelist.json", JSON.stringify(filelist, null, 4));
    }
    else {
        reslist = JSON.parse(await readFullTextFile(resroot.dbg_path + "reslist.json"));
        screentable = JSON.parse(await readFullTextFile(resroot.dbg_path + "screentable.json"));
    }

    analyze_resources();
    pack_resources();

    await writeFullTextFile(resroot.dbg_path + "pagelist.json", JSON.stringify(pagelist, null, 4));
    await writeFullTextFile(resroot.dbg_path + "filelist.json", JSON.stringify(filelist, null, 4));
    await writeFullTextFile(resroot.dbg_path + "reslist.json", JSON.stringify(reslist, null, 4));

    await write_resource_pages();
    await write_screen_list();
}

run();
