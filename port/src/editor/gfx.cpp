#include "gfx.hpp"

#include "level.hpp"

namespace editor {

namespace {

constexpr uint32_t kGfxBase = 0x1EE00;  // GRAPHICS.BIN (PHIL_02) is loaded here
constexpr uint32_t kMainBase = 0x7E00;  // MAIN.BIN (PHIL_01)
constexpr uint32_t kPointerTable = 0x1B5A0;
constexpr int kSheetW = 320, kSheetH = 244;
constexpr uint32_t kPlaneSize = kSheetW / 8 * kSheetH;  // $2620: planes one after another on the disk

uint32_t be32(const std::vector<uint8_t>& d, size_t o) {
    return o + 4 <= d.size() ? uint32_t(d[o]) << 24 | uint32_t(d[o + 1]) << 16 | uint32_t(d[o + 2]) << 8 | d[o + 3] : 0;
}

struct Src { int sheet, x, y; };

}  // namespace

int TileGfx::imageFor(int code) {
    switch (code) {
    case T_SNIKSNAK: return IMG_SNIKSNAK;
    case T_ELECTRON: return IMG_ELECTRON;
    case T_BUG: return IMG_BUG;
    default: return code >= 0 && code < T_COUNT ? code : T_SPACE;
    }
}

bool TileGfx::load(const amiga::GameFiles& files, std::string* err) {
    std::vector<uint8_t> main = files.read("PHIL_01"), gfx = files.read("PHIL_02");
    // resource pointers: the first entry is an address, the next ones are sizes
    size_t o = kPointerTable - kMainBase;
    uint32_t tilesAddr = be32(main, o), p0 = tilesAddr;
    tilesAddr += be32(main, o + 4);                // [1] tile sheets
    uint32_t palAddr = tilesAddr + be32(main, o + 8);  // [2] game palette
    (void)p0;
    if (tilesAddr < kGfxBase || palAddr < kGfxBase || palAddr + 32 > kGfxBase + gfx.size() ||
        tilesAddr + 8 * kPlaneSize > kGfxBase + gfx.size()) {
        if (err) *err = "не удалось найти графику тайлов в GRAPHICS.BIN";
        return false;
    }
    uint32_t tiles = tilesAddr - kGfxBase, pal = palAddr - kGfxBase;
    uint32_t rgb[16];
    for (int i = 0; i < 16; i++) {
        unsigned v = unsigned(gfx[pal + 2u * unsigned(i)] << 8 | gfx[pal + 2u * unsigned(i) + 1]);
        rgb[i] = 0xFF000000u | ((v >> 8) & 15) * 0x110000u | ((v >> 4) & 15) * 0x1100u | (v & 15) * 0x11u;
    }
    auto pixel = [&](int sheet, int x, int y) {
        uint32_t base = tiles + uint32_t(sheet) * 4 * kPlaneSize + uint32_t(y) * (kSheetW / 8) + uint32_t(x) / 8;
        int c = 0;
        for (int p = 0; p < 4; p++)
            if (gfx[base + uint32_t(p) * kPlaneSize] & (0x80 >> (x & 7))) c |= 1 << p;
        return rgb[c];
    };
    std::vector<Src> src;
    for (int code = 0; code < T_COUNT; code++) src.push_back({0, (code % 20) * 16, (code / 20) * 16});
    src.push_back({1, 0, 224});    // snik snak
    src.push_back({1, 48, 208});   // electron
    src.push_back({0, 272, 228});  // bug (base with a spark)
    atlas_.assign(size_t(IMG_COUNT * kTilePx * kTilePx), 0);
    avg_.assign(IMG_COUNT, 0);
    for (int i = 0; i < IMG_COUNT; i++) {
        unsigned r = 0, g = 0, b = 0;
        for (int y = 0; y < kTilePx; y++)
            for (int x = 0; x < kTilePx; x++) {
                uint32_t c = pixel(src[size_t(i)].sheet, src[size_t(i)].x + x, src[size_t(i)].y + y);
                atlas_[size_t(y * atlasWidth() + i * kTilePx + x)] = c;
                r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255;
            }
        avg_[size_t(i)] = 0xFF000000u | (r / 256) << 16 | (g / 256) << 8 | (b / 256);
    }
    return true;
}

}  // namespace editor
