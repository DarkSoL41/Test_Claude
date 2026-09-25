// Checks of the editor's level model against the original levels:
//   level_test <data/LEVELS.DAT>
#include <cstdio>
#include <vector>

#include "../src/editor/level.hpp"

using namespace editor;

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "data/LEVELS.DAT";
    LevelSet set;
    std::string err;
    if (!set.load(path, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    int fails = 0;
    std::vector<uint8_t> orig;
    LevelSet::readFile(path, orig);
    for (int i = 0; i < kLevelCount; i++) {
        Level l = set.levels[size_t(i)];
        bool valid = false;
        Camera eff = l.effectiveCamera(&valid);
        if (!valid) { std::printf("level %d: original camera invalid\n", i + 1); fails++; }
        // normalizing keeps what the game shows
        Level n = l;
        n.normalize();
        bool v2 = false;
        Camera eff2 = n.effectiveCamera(&v2);
        if (!v2 || eff2.x != eff.x || eff2.y != eff.y) {
            std::printf("level %d: camera %d,%d -> %d,%d after normalize\n", i + 1, eff.x, eff.y, eff2.x, eff2.y);
            fails++;
        }
        // special ports that the game can reach are kept
        for (const SpecialPort& p : l.specialPorts()) {
            if (p.cell < kMapW * kMapH && isSpecialPort(l.b[size_t(p.cell)])) {
                int k = n.findPort(p.cell);
                auto np = n.specialPorts();
                if (k < 0 || np[size_t(k)].gravity != p.gravity || np[size_t(k)].freezeZonks != p.freezeZonks ||
                    np[size_t(k)].freezeEnemies != p.freezeEnemies) {
                    std::printf("level %d: special port at %d lost\n", i + 1, p.cell);
                    fails++;
                }
            }
        }
        // map and header fields other than camera / ports untouched
        for (int k = 0; k < 1440; k++) if (n.b[size_t(k)] != l.b[size_t(k)]) { std::printf("level %d: map changed\n", i + 1); fails++; break; }
        for (int k = 1444; k < 1471; k++)
            if (k != 1445 && n.b[size_t(k)] != l.b[size_t(k)]) { std::printf("level %d: header byte %d changed\n", i + 1, k); fails++; break; }
        for (const Issue& is : l.validate())
            if (is.level == Issue::Error) std::printf("level %d: %s\n", i + 1, is.text.c_str());
    }
    // saving without edits keeps the file byte for byte
    set.save("level_test_out.dat", &err);
    std::vector<uint8_t> again;
    LevelSet::readFile("level_test_out.dat", again);
    std::remove("level_test_out.dat");
    if (again != orig) { std::printf("round trip changed the file\n"); fails++; }
    std::printf(fails ? "FAILED: %d\n" : "OK: level model checks passed\n", fails);
    return fails ? 1 : 0;
}
