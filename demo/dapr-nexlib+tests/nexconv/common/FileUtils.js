const { open, readdir, stat } = require('fs').promises;

let logfun = console.log

function disableLogging()
{
    logfun = () => {}
}

async function readFullTextFile(filePath) {
    logfun('readFullTextFile:', filePath);
    try {
        const file = await open(filePath);
        const txt = await file.readFile({encoding:'utf8'});
        file.close();
        return txt;
    }
    catch(err) {
        // logfun(err);
        logfun('cannot read from', filePath);
        return null;
    }
}

async function writeFullTextFile(filePath, fullText) {
    logfun('writeFullTextFile:', filePath);
    const file = await open(filePath, 'w');
    await file.writeFile(fullText, {encoding:'utf8'});
    file.close();
}

async function readFullBinaryFile(filePath) {
    logfun('readFullBinaryFile:', filePath);
    try {
        const file = await open(filePath);
        const buf = await file.readFile();
        file.close();
        return buf;
    }
    catch(err) {
        // logfun(err);
        logfun('cannot read from', filePath);
        return null;
    }
}

async function writeFullBinaryFile(filePath, fullData) {
    logfun('writeFullBinaryFile:', filePath);
    const file = await open(filePath, 'w');
    await file.writeFile(Buffer.from(fullData));
    file.close();
}

async function readFileAsBase64(filePath) {
    logfun('readFileAsBase64:', filePath);
    try {
        const file = await open(filePath);
        const buf = await file.readFile();
        file.close();
        return buf.toString('base64');
    }
    catch(err) {
        logfun(err);
        return null;
    }
}

const prefsPath = 'preferences.json';

async function readPreferences() {
    try {
        const txt = await readFullTextFile(null, prefsPath);
        return JSON.parse(txt);
    }
    catch(err) {
        logfun(err);
        return null;
    }
}

async function writePreferences(prefs) {
    const text = JSON.stringify(prefs);
    await writeFullTextFile(null, prefsPath, text);
}

function registerFileUtils()
{
    ipcMain.handle('dialogOpenFile', dialogOpenFile);
    ipcMain.handle('dialogSaveFile', dialogSaveFile);
    ipcMain.handle('readFullTextFile', readFullTextFile);
    ipcMain.handle('writeFullTextFile', writeFullTextFile);
    ipcMain.handle('readFullBinaryFile', readFullBinaryFile);
    ipcMain.handle('writeFullBinaryFile', writeFullBinaryFile);
    ipcMain.handle('readFileAsBase64', readFileAsBase64);
    ipcMain.handle('readPreferences', readPreferences);
    ipcMain.handle('writePreferences', writePreferences);

    logfun('FileUtils registered.');
}

module.exports = {
    disableLogging,
    readFullTextFile, writeFullTextFile,
    readFullBinaryFile, writeFullBinaryFile
};