// Game data files: the files of the original disk, read either from the
// extracted data folder or directly from the ADF image.
//
// Data folder (created by extractTo, see docs/06-formats.md):
//   INTRO.BIN     PHIL_00  intro program and its graphics (loaded at $54000)
//   MAIN.BIN      PHIL_01  main program: tables, texts (loaded at $7E00)
//   GRAPHICS.BIN  PHIL_02  graphics, music, samples, demo (loaded at $1EE00)
//   HISCORE.BIN   PHIL_03  hiscore file as shipped on the disk
//   LEVELS.DAT    PHIL_10 … PHIL_7E  111 levels, 1536 bytes each
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "adf.hpp"

namespace amiga {

constexpr int kLevelCount = 111;
constexpr size_t kLevelSize = 1536;       // the part of a level file the game uses
constexpr size_t kLevelFileSize = 2048;   // level file on the disk

class GameFiles {
public:
    // folder with the extracted files (all of them must be present)
    bool openDir(const std::string& dir);
    // original disk image
    bool openAdf(const std::string& path);

    // contents of a disk file by its name on the disk ("PHIL_00" … "PHIL_7E"),
    // empty if unknown
    std::vector<uint8_t> read(const std::string& diskName) const;

    // write the data folder (from the currently open source)
    bool extractTo(const std::string& dir) const;

    const std::string& error() const { return err_; }

private:
    static int levelNumber(const std::string& diskName);  // 1…111, 0 if not a level
    Adf adf_;
    bool fromAdf_ = false;
    std::map<std::string, std::vector<uint8_t>> files_;  // disk name -> contents
    std::vector<uint8_t> levels_;                        // LEVELS.DAT
    mutable std::string err_;
};

}  // namespace amiga
