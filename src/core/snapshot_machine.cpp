#include "core/snapshot_machine.h"

#include "core/sna_loader.h"
#include "core/szx_loader.h"
#include "core/z80_loader.h"

#include <fstream>
#include <iterator>

bool snapshot_file_loadable(const std::string& path, std::string& why)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { why = "cannot read the file"; return false; }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    const std::string ext = snapshot_file_ext(path);
    const SnapshotMachine m = snapshot_machine(bytes, ext);
    if (!m.ok) { why = m.why; return false; }
    bool parsed = false;
    if (ext == "sna")      { SnaLoader l; parsed = l.load_from_buffer(bytes, path); }
    else if (ext == "szx") { SzxLoader l; parsed = l.load_from_buffer(bytes, path); }
    else                   { Z80Loader l; parsed = l.load_from_buffer(bytes); }
    if (!parsed) { why = "the file is damaged (see the log)"; return false; }
    why.clear();
    return true;
}
