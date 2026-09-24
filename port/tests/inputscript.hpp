// Scripted input for automated runs. One command per line:
//   <frame> joy <UDLRF...|->      joystick state from this frame on ('-' = released)
//   <frame> mouse <dx> <dy> <L> <R>  move mouse and set buttons (0/1)
//   <frame> key <rawcode> <0|1>   Amiga raw key code pressed (1) / released (0)
// Lines starting with '#' are comments.
#pragma once
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/amiga/amiga.hpp"

struct InputEvent {
    uint64_t frame;
    std::string kind;
    std::string a;
    int x = 0, y = 0, l = 0, r = 0;
};

class InputScript {
public:
    bool load(const std::string& path) {
        std::ifstream f(path);
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream s(line);
            InputEvent e;
            s >> e.frame >> e.kind;
            if (e.kind == "joy") s >> e.a;
            else if (e.kind == "mouse") s >> e.x >> e.y >> e.l >> e.r;
            else if (e.kind == "key") s >> e.x >> e.y;
            ev_.push_back(e);
        }
        return true;
    }
    // apply all events scheduled for this frame
    void apply(uint64_t frame, amiga::Amiga& hw) {
        for (auto& e : ev_) {
            if (e.frame != frame) continue;
            if (e.kind == "joy") {
                auto has = [&](char c) { return e.a.find(c) != std::string::npos; };
                hw.setJoystick(has('U'), has('D'), has('L'), has('R'), has('F'));
            } else if (e.kind == "mouse") {
                hw.moveMouse(e.x, e.y);
                hw.setMouseButtons(e.l, e.r);
            } else if (e.kind == "key") {
                hw.keyEvent(uint8_t(e.x), e.y != 0);
            }
        }
    }
    bool empty() const { return ev_.empty(); }

private:
    std::vector<InputEvent> ev_;
};

inline void writePPM(const std::string& path, const uint32_t* fb, int w, int h) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        unsigned char px[3] = {uint8_t(fb[i] >> 16), uint8_t(fb[i] >> 8), uint8_t(fb[i])};
        std::fwrite(px, 1, 3, f);
    }
    std::fclose(f);
}
