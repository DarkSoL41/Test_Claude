// Level model of the editor: the 1536-byte level record of the Amiga version
// (same size and cell codes as the PC LEVELS.DAT), with the Amiga-specific
// rules found in the analysis (docs/06-formats.md, docs/08-research-notes.md).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace editor {

constexpr int kMapW = 60, kMapH = 24;
constexpr int kLevelSize = 1536;
constexpr int kLevelCount = 111;
constexpr int kTitleLen = 23;
constexpr int kMaxSpecialPorts = 10;  // full records that fit into the level file

enum Tile : uint8_t {
    T_SPACE = 0, T_ZONK = 1, T_BASE = 2, T_MURPHY = 3, T_INFOTRON = 4, T_RAMCHIP = 5,
    T_HARDWARE = 6, T_EXIT = 7, T_ORANGE_DISK = 8,
    T_PORT_RIGHT = 9, T_PORT_DOWN = 10, T_PORT_LEFT = 11, T_PORT_UP = 12,
    T_SPORT_RIGHT = 13, T_SPORT_DOWN = 14, T_SPORT_LEFT = 15, T_SPORT_UP = 16,
    T_SNIKSNAK = 17, T_YELLOW_DISK = 18, T_TERMINAL = 19, T_RED_DISK = 20,
    T_PORT_VERT = 21, T_PORT_HORZ = 22, T_PORT_CROSS = 23, T_ELECTRON = 24, T_BUG = 25,
    T_RAM_LEFT = 26, T_RAM_RIGHT = 27, T_HW_FIRST = 28, T_HW_LAST = 37, T_RAM_TOP = 38, T_RAM_BOTTOM = 39,
    T_COUNT = 40
};

struct TileInfo {
    const char* nameEn;
    const char* hintEn;    // one-line description
    const char* nameRu;
    const char* hintRu;
    const char* name() const;  // in the interface language
    const char* hint() const;
};
const TileInfo& tileInfo(int code);
bool isHardware(int code);      // indestructible wall: 6 and its variants 28..37
bool isSpecialPort(int code);   // 13..16
int mirrorTileH(int code);      // tile after a left-right mirror
int mirrorTileV(int code);      // tile after an up-down mirror

struct SpecialPort {
    int cell = 0;               // y * 60 + x
    uint8_t gravity = 0;        // 1 = gravity on after passing
    uint8_t freezeZonks = 0;    // 2 = zonks frozen (see header byte 1469)
    uint8_t freezeEnemies = 0;  // non-zero = snik snaks / electrons frozen
};

struct Camera { int x = 0, y = 0; };

struct Issue {
    enum Level { Error, Warning, Info } level;
    std::string text;
    int x = -1, y = -1;  // cell to show, -1 if none
};

class Level {
public:
    std::array<uint8_t, kLevelSize> b{};

    static Level blank();  // hardware border, Murphy and exit

    int tile(int x, int y) const { return b[size_t(y * kMapW + x)]; }
    void setTile(int x, int y, int v) { b[size_t(y * kMapW + x)] = uint8_t(v); }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < kMapW && y < kMapH; }

    std::string title() const;                 // 23 characters, trailing spaces kept
    void setTitle(const std::string& t);       // upper-cased, padded / cut to 23
    bool gravity() const { return b[1444] != 0; }
    void setGravity(bool on) { b[1444] = on ? 1 : 0; }
    int freezeZonks() const { return b[1469]; }
    void setFreezeZonks(int v) { b[1469] = uint8_t(v); }
    int infotronsNeeded() const { return b[1470]; }
    void setInfotronsNeeded(int v) { b[1470] = uint8_t(v); }
    Camera storedCamera() const;
    void setStoredCamera(Camera c);
    Camera recommendedCamera() const;          // centred on Murphy, clamped to the map
    Camera effectiveCamera(bool* valid) const; // what the game ends up showing
    void fixCamera();                          // store the effective camera, or the recommended one if it is off the map

    // records 0..9 that are in use (non-zero cell offset), in file order
    std::vector<SpecialPort> specialPorts() const;
    void setSpecialPorts(const std::vector<SpecialPort>& ports);  // also count byte, zero rest
    int findPort(int cell) const;              // index in specialPorts(), -1 if none

    int count(int code) const;
    int murphyCell() const;                    // first Murphy, -1 if none

    // make the record safe for the Amiga (done for every level edited in the
    // editor): camera (unless autoCamera is off: then the stored camera is
    // kept as set by hand), special port records only for special port cells,
    // unused records and bytes 1532..1535 zero, count byte right
    void normalize(bool autoCamera = true);

    std::vector<Issue> validate() const;
};

class LevelSet {
public:
    std::array<Level, kLevelCount> levels;
    std::array<bool, kLevelCount> edited{};

    bool load(const std::string& path, std::string* err);
    bool save(const std::string& path, std::string* err, bool autoCamera = true);  // normalizes the edited levels
    static bool readFile(const std::string& path, std::vector<uint8_t>& out);
    static bool writeFile(const std::string& path, const std::vector<uint8_t>& data);
};

}  // namespace editor
