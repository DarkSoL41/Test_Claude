// Tile graphics of the game for the editor, taken from the game data
// (GRAPHICS.BIN + the resource pointer table in MAIN.BIN), exactly as the game
// draws a level: cell code N = tile N % 20, row N / 20 of the first tile sheet.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "amiga/gamefiles.hpp"

namespace editor {

constexpr int kTilePx = 16;
// extra images after the 40 cell codes: the
// real look of objects that the level picture shows as a placeholder or as
// something else, and the special ports recoloured blue (the game draws them
// like plain ports)
enum ExtraImage { IMG_SNIKSNAK = 40, IMG_ELECTRON = 41, IMG_BUG = 42, IMG_SPORT_FIRST = 43, IMG_COUNT = 47 };

class TileGfx {
public:
    bool load(const amiga::GameFiles& files, std::string* err);
    // ARGB8888 atlas: IMG_COUNT images of 16x16 in one row
    const std::vector<uint32_t>& atlas() const { return atlas_; }
    int atlasWidth() const { return IMG_COUNT * kTilePx; }
    uint32_t averageColor(int image) const { return avg_[size_t(image)]; }
    // image to show in the editor for a cell code (special ports blue)
    static int imageFor(int code);
    // the game's own picture of the cell (for the exported level picture)
    static int gameImageFor(int code);

private:
    std::vector<uint32_t> atlas_;
    std::vector<uint32_t> avg_;
};

}  // namespace editor
