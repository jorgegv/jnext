#pragma once
// Copy a program's directory out of a FAT32 SD-card image (GH #510): the browser page makes the
// NextZXOS card in memory, and a program on it (a game's NEX with its data files) has to be
// served to the esxDOS stand-in from a host directory. FatFs reads the card (it is the one that
// wrote it); only the subtree needed is read, so the card is never held whole in memory.
#include <string>

namespace card_extract {

/// Copy the directory that holds `nex` (an absolute path on the card, any case, ending .nex, not
/// in the card's root), with every file and directory below it, out of the FAT32 card image `card`
/// into the new host directory `dest` (it must not exist). `out_nex` = the copied NEX's host path
/// (the name as stored on the card, long names kept). The card is never written. All or nothing:
/// on any failure `dest` is removed and `why` names the path.
bool copy_program_dir(const std::string& card, const std::string& nex, const std::string& dest,
                      std::string& out_nex, std::string& why);

}  // namespace card_extract
