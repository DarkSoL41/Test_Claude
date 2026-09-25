// Supaplex (Amiga) — Windows/Linux front end on SDL2.
//
// Runs the translated game on the emulated Amiga machine. Every time the
// virtual beam completes a frame, this front end presents the picture, feeds
// the audio produced by Paula to SDL, reads the input devices and waits for
// the next PAL frame (50 Hz).
//
// Controls (as on the Amiga: joystick in port 2, mouse in port 1)
//   game controller : the joystick (d-pad / left stick, A B X Y = fire)
//   mouse           : the Amiga mouse; the Windows cursor leads the game's
//                     pointer, nothing is captured
//   keyboard        : in a level, cursor keys + Space act as the joystick
//                     (a PC has no joystick port); while the game asks for a
//                     player name the keys go to the Amiga keyboard. Elsewhere
//                     the keyboard does nothing, like in the original.
//   F11 or Alt+Enter : fullscreen,  Pause : pause,  F12 or window close : quit
#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "amiga/gamefiles.hpp"
#include "amiga/amiga.hpp"
#include "amiga/paula.hpp"
#include "game/hiscores.hpp"
#include "game/runtime.hpp"

namespace {

constexpr int kSampleRate = 48000;
// PAL frame: 313 lines of 227.5 colour clocks at 3.546895 MHz
constexpr double kFrameSeconds = 313.0 * 227.5 / 3546895.0;

int amigaKey(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_GRAVE: return 0x00;
    case SDL_SCANCODE_1: return 0x01; case SDL_SCANCODE_2: return 0x02; case SDL_SCANCODE_3: return 0x03;
    case SDL_SCANCODE_4: return 0x04; case SDL_SCANCODE_5: return 0x05; case SDL_SCANCODE_6: return 0x06;
    case SDL_SCANCODE_7: return 0x07; case SDL_SCANCODE_8: return 0x08; case SDL_SCANCODE_9: return 0x09;
    case SDL_SCANCODE_0: return 0x0A; case SDL_SCANCODE_MINUS: return 0x0B; case SDL_SCANCODE_EQUALS: return 0x0C;
    case SDL_SCANCODE_BACKSLASH: return 0x0D; case SDL_SCANCODE_KP_0: return 0x0F;
    case SDL_SCANCODE_Q: return 0x10; case SDL_SCANCODE_W: return 0x11; case SDL_SCANCODE_E: return 0x12;
    case SDL_SCANCODE_R: return 0x13; case SDL_SCANCODE_T: return 0x14; case SDL_SCANCODE_Y: return 0x15;
    case SDL_SCANCODE_U: return 0x16; case SDL_SCANCODE_I: return 0x17; case SDL_SCANCODE_O: return 0x18;
    case SDL_SCANCODE_P: return 0x19; case SDL_SCANCODE_LEFTBRACKET: return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B; case SDL_SCANCODE_KP_1: return 0x1D;
    case SDL_SCANCODE_KP_2: return 0x1E; case SDL_SCANCODE_KP_3: return 0x1F;
    case SDL_SCANCODE_A: return 0x20; case SDL_SCANCODE_S: return 0x21; case SDL_SCANCODE_D: return 0x22;
    case SDL_SCANCODE_F: return 0x23; case SDL_SCANCODE_G: return 0x24; case SDL_SCANCODE_H: return 0x25;
    case SDL_SCANCODE_J: return 0x26; case SDL_SCANCODE_K: return 0x27; case SDL_SCANCODE_L: return 0x28;
    case SDL_SCANCODE_SEMICOLON: return 0x29; case SDL_SCANCODE_APOSTROPHE: return 0x2A;
    case SDL_SCANCODE_KP_4: return 0x2D; case SDL_SCANCODE_KP_5: return 0x2E; case SDL_SCANCODE_KP_6: return 0x2F;
    case SDL_SCANCODE_NONUSBACKSLASH: return 0x30;
    case SDL_SCANCODE_Z: return 0x31; case SDL_SCANCODE_X: return 0x32; case SDL_SCANCODE_C: return 0x33;
    case SDL_SCANCODE_V: return 0x34; case SDL_SCANCODE_B: return 0x35; case SDL_SCANCODE_N: return 0x36;
    case SDL_SCANCODE_M: return 0x37; case SDL_SCANCODE_COMMA: return 0x38; case SDL_SCANCODE_PERIOD: return 0x39;
    case SDL_SCANCODE_SLASH: return 0x3A; case SDL_SCANCODE_KP_PERIOD: return 0x3C;
    case SDL_SCANCODE_KP_7: return 0x3D; case SDL_SCANCODE_KP_8: return 0x3E; case SDL_SCANCODE_KP_9: return 0x3F;
    case SDL_SCANCODE_SPACE: return 0x40; case SDL_SCANCODE_BACKSPACE: return 0x41; case SDL_SCANCODE_TAB: return 0x42;
    case SDL_SCANCODE_KP_ENTER: return 0x43; case SDL_SCANCODE_RETURN: return 0x44; case SDL_SCANCODE_ESCAPE: return 0x45;
    case SDL_SCANCODE_DELETE: return 0x46; case SDL_SCANCODE_KP_MINUS: return 0x4A;
    case SDL_SCANCODE_UP: return 0x4C; case SDL_SCANCODE_DOWN: return 0x4D;
    case SDL_SCANCODE_RIGHT: return 0x4E; case SDL_SCANCODE_LEFT: return 0x4F;
    case SDL_SCANCODE_F1: return 0x50; case SDL_SCANCODE_F2: return 0x51; case SDL_SCANCODE_F3: return 0x52;
    case SDL_SCANCODE_F4: return 0x53; case SDL_SCANCODE_F5: return 0x54; case SDL_SCANCODE_F6: return 0x55;
    case SDL_SCANCODE_F7: return 0x56; case SDL_SCANCODE_F8: return 0x57; case SDL_SCANCODE_F9: return 0x58;
    case SDL_SCANCODE_F10: return 0x59; case SDL_SCANCODE_KP_DIVIDE: return 0x5C;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x5D; case SDL_SCANCODE_KP_PLUS: return 0x5E;
    case SDL_SCANCODE_LSHIFT: return 0x60; case SDL_SCANCODE_RSHIFT: return 0x61;
    case SDL_SCANCODE_CAPSLOCK: return 0x62; case SDL_SCANCODE_LCTRL: return 0x63;
    case SDL_SCANCODE_LALT: return 0x64; case SDL_SCANCODE_RALT: return 0x65;
    case SDL_SCANCODE_LGUI: return 0x66; case SDL_SCANCODE_RGUI: return 0x67;
    default: return -1;
    }
}

class Frontend : public amiga::Host {
public:
    Frontend(amiga::Amiga& hw, amiga::Paula& paula) : hw_(hw), paula_(paula) {}

    bool init(int scale, bool fullscreen) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
            std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return false;
        }
        int w = 320 * scale, h = 256 * scale;
        window_ = SDL_CreateWindow("Supaplex (Amiga)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (testLevel ? SDL_WINDOW_HIDDEN : 0) |
                                       (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
        if (!window_) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer_) { std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return false; }
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                     amiga::kOutWidth, amiga::kOutHeight);
        fullscreen_ = fullscreen;

        SDL_AudioSpec want{}, have{};
        want.freq = kSampleRate;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        audio_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (audio_) SDL_PauseAudioDevice(audio_, 0);
        else std::fprintf(stderr, "no audio: %s\n", SDL_GetError());

        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (SDL_IsGameController(i)) { pad_ = SDL_GameControllerOpen(i); if (pad_) break; }
        }
        SDL_ShowCursor(SDL_DISABLE);
        nextFrame_ = SDL_GetPerformanceCounter();
        return true;
    }

    ~Frontend() override {
        if (pad_) SDL_GameControllerClose(pad_);
        if (audio_) SDL_CloseAudioDevice(audio_);
        if (texture_) SDL_DestroyTexture(texture_);
        if (renderer_) SDL_DestroyRenderer(renderer_);
        if (window_) SDL_DestroyWindow(window_);
        SDL_Quit();
    }

    void onInterrupt(int level) override { game::requestInterrupt(level); }

    long quitAfter = -1;          // debugging: quit after this many frames
    int testLevel = 0;            // level editor: play this level (1..111), then quit
    std::string shotPath;         // debugging: write the last frame here (PPM)

    void onFrame() override {
        if (quitAfter >= 0 && long(hw_.frameCount()) >= quitAfter) {
            if (!shotPath.empty()) writeShot();
            throw game::QuitGame{};
        }
        if (testLevel && testState_ < 3) {
            // level test from the editor: run through the intro and the menu
            // at full speed, without picture and sound
            if (++testFrames_ % 25 == 0) { SDL_Event e; while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) throw game::QuitGame{}; }
            trackLevel();
            driveTest();
            paula_.samples().clear();
            nextFrame_ = SDL_GetPerformanceCounter();
            return;
        }
        handleEvents();
        while (paused_) {
            present();
            SDL_Delay(20);
            handleEvents();
            nextFrame_ = SDL_GetPerformanceCounter();
        }
        present();
        queueAudio();
        pace();
    }

private:
    void writeShot() {
        std::FILE* f = std::fopen(shotPath.c_str(), "wb");
        if (!f) return;
        std::fprintf(f, "P6\n%d %d\n255\n", amiga::kOutWidth, amiga::kOutHeight);
        const uint32_t* fb = hw_.frameBuffer();
        for (int i = 0; i < amiga::kOutWidth * amiga::kOutHeight; i++) {
            unsigned char px[3] = {uint8_t(fb[i] >> 16), uint8_t(fb[i] >> 8), uint8_t(fb[i])};
            std::fwrite(px, 1, 3, f);
        }
        std::fclose(f);
    }

    void present() {
        SDL_UpdateTexture(texture_, nullptr, hw_.frameBuffer(), amiga::kOutWidth * 4);
        int ww, wh;
        SDL_GetRendererOutputSize(renderer_, &ww, &wh);
        // 320x256 PAL lowres picture on a 4:3 display
        double aspect = 4.0 / 3.0 * (256.0 / 256.0);
        int dw = ww, dh = int(ww / aspect);
        if (dh > wh) { dh = wh; dw = int(wh * aspect); }
        dst_ = SDL_Rect{(ww - dw) / 2, (wh - dh) / 2, dw, dh};
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        SDL_RenderCopy(renderer_, texture_, nullptr, &dst_);
        SDL_RenderPresent(renderer_);
    }

    void queueAudio() {
        auto& s = paula_.samples();
        if (audio_ && !s.empty()) {
            const Uint32 maxQueued = Uint32(kSampleRate * 4 * 0.15);  // keep latency below 150 ms
            if (SDL_GetQueuedAudioSize(audio_) > maxQueued) SDL_ClearQueuedAudio(audio_);
            SDL_QueueAudio(audio_, s.data(), Uint32(s.size() * sizeof(int16_t)));
        }
        s.clear();
    }

    void pace() {
        const Uint64 freq = SDL_GetPerformanceFrequency();
        nextFrame_ += Uint64(kFrameSeconds * double(freq));
        Uint64 now = SDL_GetPerformanceCounter();
        if (now > nextFrame_ + freq / 4) nextFrame_ = now;  // fell behind: resynchronise
        while (now < nextFrame_) {
            Uint64 left = (nextFrame_ - now) * 1000 / freq;
            if (left > 2) SDL_Delay(Uint32(left - 1));
            now = SDL_GetPerformanceCounter();
        }
    }

    // Level test for the editor. The menu is driven like a player would:
    // wait until it reads the mouse, select the level ($140DC; the test
    // player may play all levels), press fire; quit when the level is over.
    void driveTest() {
        switch (testState_) {
        case 0: {
            hw_.moveMouse((testFrames_ & 1) ? 1 : -1, 0);  // the menu reads the mouse counters
            uint8_t last = hw_.chip()[0x112EA];
            if (testFrames_ > 2 && last != lastMouseRead_ && hw_.chipL(0x113D6) != 0) testState_ = 1;
            lastMouseRead_ = last;
            if (testFrames_ > 5000) fail("menu not reached");
            break;
        }
        case 1:
            hw_.chip()[0x140DC] = 0;
            hw_.chip()[0x140DD] = uint8_t(testLevel - 1);
            hw_.setJoystick(false, false, false, false, true);
            if (++testFire_ > 4) { hw_.setJoystick(false, false, false, false, false); testState_ = 2; }
            break;
        case 2:
            if (inLevel_) {
                testState_ = 3;
                SDL_ShowWindow(window_);
                SDL_RaiseWindow(window_);
            }
            if (++testWait_ > 3000) fail("level did not start");
            break;
        default:
            if (!inLevel_) throw game::QuitGame{};
            break;
        }
    }
    [[noreturn]] void fail(const char* why) {
        std::fprintf(stderr, "level test: %s\n", why);
        throw game::QuitGame{};
    }

    // The game installs its keyboard interrupt handler ($10214 in vector $68)
    // only while a player name is typed in.
    bool nameEntry() const { return hw_.chipL(0x68) == 0x10214; }

    // A level (or the demo) runs while game_frame counts v_frame_div50
    // ($11294) every frame (nothing else writes it); the first count comes
    // one frame before the level first looks at the mouse button.
    void trackLevel() {
        uint8_t t = hw_.chip()[0x11294];
        bool counting = t != lastTick_;
        lastTick_ = t;
        if (counting) {
            if (!inLevel_) {
                // On the Amiga the level is loaded from floppy after the click
                // on OK (or the fire button): by the time it runs, the button
                // is up. Here it starts at once, so a button still held from
                // the menu must not reach the level (left button = give up).
                holdLmb_ = lmb_;
                holdRmb_ = rmb_;
                holdFire_ = true;
            }
            inLevel_ = true;
            idle_ = 0;
        } else if (inLevel_ && ++idle_ > 5) {
            inLevel_ = false;
        }
    }

    void updateJoystick() {
        bool up = false, down = false, left = false, right = false, fire = false;
        if (inLevel_ && !nameEntry()) {
            up = keyDown(SDL_SCANCODE_UP);
            down = keyDown(SDL_SCANCODE_DOWN);
            left = keyDown(SDL_SCANCODE_LEFT);
            right = keyDown(SDL_SCANCODE_RIGHT);
            fire = keyDown(SDL_SCANCODE_SPACE);
        }
        if (pad_) {
            auto b = [&](SDL_GameControllerButton x) { return SDL_GameControllerGetButton(pad_, x) != 0; };
            const int dz = 12000;
            int ax = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
            int ay = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
            up = up || b(SDL_CONTROLLER_BUTTON_DPAD_UP) || ay < -dz;
            down = down || b(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ay > dz;
            left = left || b(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || ax < -dz;
            right = right || b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax > dz;
            fire = fire || b(SDL_CONTROLLER_BUTTON_A) || b(SDL_CONTROLLER_BUTTON_B) ||
                   b(SDL_CONTROLLER_BUTTON_X) || b(SDL_CONTROLLER_BUTTON_Y);
        }
        if (!fire) holdFire_ = false;
        hw_.setJoystick(up, down, left, right, fire && !holdFire_);
    }

    void updateMouse() {
        if (!lmb_) holdLmb_ = false;
        if (!rmb_) holdRmb_ = false;
        hw_.setMouseButtons(lmb_ && !holdLmb_, rmb_ && !holdRmb_);

        // The game moves its pointer by the change of the mouse counters and
        // keeps the position in $112EC/$112EE (in counts, pixel = count / 2).
        // Feed the counters so that the pointer goes to the Windows cursor.
        int ox = testX_, oy = testY_;
        if (!testMode_) {
            if (SDL_GetMouseFocus() != window_ || dst_.w <= 0 || dst_.h <= 0) return;
            int mx, my, ww, wh, rw, rh;
            SDL_GetMouseState(&mx, &my);
            SDL_GetWindowSize(window_, &ww, &wh);
            SDL_GetRendererOutputSize(renderer_, &rw, &rh);
            double px = mx * double(rw) / std::max(ww, 1), py = my * double(rh) / std::max(wh, 1);
            ox = std::clamp(int((px - dst_.x) * amiga::kOutWidth / dst_.w), 0, amiga::kOutWidth - 1);
            oy = std::clamp(int((py - dst_.y) * amiga::kOutHeight / dst_.h), 0, amiga::kOutHeight - 1);
        }
        // pointer sprite: x = lowres pixel + 1 (display window starts at $81), y = line - $2C
        int targetX = (ox / 2 + 1) * 2, targetY = oy * 2;
        const uint8_t* c = hw_.chip();
        auto feed = [](int target, int pos, uint8_t counter, uint8_t lastRead) {
            int pending = int8_t(uint8_t(counter - lastRead));   // not yet seen by the game
            int want = std::clamp(target - pos, -100, 100);       // the game takes up to ±127 per read
            return want - pending;
        };
        int dx = feed(targetX, int16_t(c[0x112EC] << 8 | c[0x112ED]), hw_.mouseCounterX(), c[0x112EA]);
        int dy = feed(targetY, int16_t(c[0x112EE] << 8 | c[0x112EF]), hw_.mouseCounterY(), c[0x112EB]);
        if (dx || dy) hw_.moveMouse(dx, dy);
    }

    void handleEvents() {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: throw game::QuitGame{};
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                bool down = e.type == SDL_KEYDOWN;
                SDL_Scancode sc = e.key.keysym.scancode;
                if (down && (sc == SDL_SCANCODE_F11 ||
                             (sc == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)))) {
                    fullscreen_ = !fullscreen_;
                    SDL_SetWindowFullscreen(window_, fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    break;
                }
                if (down && sc == SDL_SCANCODE_F12) throw game::QuitGame{};
                if (down && sc == SDL_SCANCODE_ESCAPE && testLevel) throw game::QuitGame{};  // back to the editor
                if (down && sc == SDL_SCANCODE_PAUSE) { paused_ = !paused_; break; }
                if (e.key.repeat) break;
                typeKey(sc, down);
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                bool d = e.type == SDL_MOUSEBUTTONDOWN;
                if (e.button.button == SDL_BUTTON_LEFT) lmb_ = d;
                if (e.button.button == SDL_BUTTON_RIGHT) rmb_ = d;
                break;
            }
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) lmb_ = rmb_ = false;
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!pad_) pad_ = SDL_GameControllerOpen(e.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (pad_ && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad_))) {
                    SDL_GameControllerClose(pad_);
                    pad_ = nullptr;
                }
                break;
            default: break;
            }
        }
        if (testMode_) applyTestInput();
        trackLevel();
        if (testLevel) driveTest();
        updateJoystick();
        updateMouse();
    }

    void typeKey(SDL_Scancode sc, bool down) {
        // keys typed outside the name entry would stay queued in the CIA and
        // pop up in the next name entry
        if (!nameEntry()) return;
        int code = amigaKey(sc);
        if (code >= 0) hw_.keyEvent(uint8_t(code), down);
    }

    bool keyDown(SDL_Scancode sc) const {
        if (testMode_) return testKeys_[sc];
        return SDL_GetKeyboardState(nullptr)[sc] != 0;
    }

public:
    // Testing without a window: physical input from a script, one event per
    // line "FRAME mouse X Y" (cursor on the 640x256 picture), "FRAME lmb 0|1",
    // "FRAME rmb 0|1", "FRAME key NAME 0|1" (SDL key name), "FRAME print ADDR"
    // (print the word at ADDR).
    bool loadTestInput(const std::string& path) {
        std::ifstream f(path);
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            TestEvent ev;
            std::istringstream ls(line);
            if (!(ls >> ev.frame >> ev.what)) continue;
            if (ev.what == "key" || ev.what == "print") ls >> ev.arg;
            ls >> ev.a >> ev.b;
            test_.push_back(ev);
        }
        testMode_ = true;
        return true;
    }

private:
    struct TestEvent { long frame = 0; std::string what, arg; int a = 0, b = 0; };

    void applyTestInput() {
        long frame = long(hw_.frameCount());
        for (const TestEvent& ev : test_) {
            if (ev.frame != frame) continue;
            if (ev.what == "mouse") { testX_ = ev.a; testY_ = ev.b; }
            else if (ev.what == "lmb") lmb_ = ev.a != 0;
            else if (ev.what == "rmb") rmb_ = ev.a != 0;
            else if (ev.what == "key") {
                SDL_Scancode sc = SDL_GetScancodeFromName(ev.arg.c_str());
                testKeys_[sc] = ev.a != 0;
                typeKey(sc, ev.a != 0);
            } else if (ev.what == "print") {
                uint32_t a = uint32_t(std::strtoul(ev.arg.c_str(), nullptr, 16));
                std::printf("frame %ld: $%X = %04X\n", frame, a, hw_.chipW(a));
            }
        }
    }

    amiga::Amiga& hw_;
    amiga::Paula& paula_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    SDL_AudioDeviceID audio_ = 0;
    SDL_GameController* pad_ = nullptr;
    SDL_Rect dst_{0, 0, 640, 512};
    Uint64 nextFrame_ = 0;
    bool fullscreen_ = false, paused_ = false, lmb_ = false, rmb_ = false;
    bool inLevel_ = false, holdLmb_ = false, holdRmb_ = false, holdFire_ = false;
    uint8_t lastTick_ = 0;
    int idle_ = 0;
    int testState_ = 0, testFire_ = 0, testWait_ = 0;
    long testFrames_ = 0;
    uint8_t lastMouseRead_ = 0;
    bool testMode_ = false;
    std::vector<TestEvent> test_;
    bool testKeys_[SDL_NUM_SCANCODES] = {};
    int testX_ = 0, testY_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    std::string adfPath, savePath, dataDir, extractDir;
    int scale = 3;
    bool fullscreen = false, originalHiscores = false, resetHiscores = false;
    long quitAfter = -1;
    std::string shot, testInput;
    int testLevel = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--adf") adfPath = next();
        else if (a == "--data") dataDir = next();
        else if (a == "--extract") extractDir = next();
        else if (a == "--save") savePath = next();
        else if (a == "--scale") scale = std::max(1, std::atoi(next().c_str()));
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--original-hiscores") originalHiscores = true;
        else if (a == "--reset-hiscores") resetHiscores = true;
        else if (a == "--frames") quitAfter = std::atol(next().c_str());
        else if (a == "--shot") shot = next();
        else if (a == "--test-input") testInput = next();
        else if (a == "--test-level") testLevel = std::atoi(next().c_str());
        else if (a == "--help" || a == "-h") {
            std::printf(
                "usage: supaplex [--scale N] [--fullscreen] [--reset-hiscores] [--original-hiscores]\n"
                "                [--data DIR] [--save FILE] [--adf IMAGE] [--extract DIR]\n"
                "Everything lives in the program's folder: the game data in data\\ (LEVELS.DAT,\n"
                "INTRO.BIN, MAIN.BIN, GRAPHICS.BIN, HISCORE.BIN) and the hiscores in hiscores.sav.\n"
                "If data\\ is missing, it is extracted once from \"Supaplex (1991).adf\" found next\n"
                "to the program (or given with --adf); --extract DIR only extracts and exits.\n"
                "A new hiscore file starts without players and records; --original-hiscores\n"
                "starts it with the disk's CRYSTAL records and players; --reset-hiscores starts over.\n");
            return 0;
        } else if (adfPath.empty()) adfPath = a;
    }
    namespace fs = std::filesystem;
    char* base = SDL_GetBasePath();
    const fs::path gameDir = fs::u8path(base ? base : "");
    if (base) SDL_free(base);
    auto fail = [&](const std::string& msg) {
        std::fprintf(stderr, "%s\n", msg.c_str());
        if (extractDir.empty()) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Supaplex", msg.c_str(), nullptr);
        return 1;
    };
    // the original disk image: only needed to create the data folder
    auto openAdf = [&](amiga::GameFiles& f) {
        if (!adfPath.empty()) return f.openAdf(adfPath);
        for (const char* c : {"Supaplex (1991).adf", "Supaplex.adf", "supaplex.adf"})
            if (f.openAdf((gameDir / c).string())) return true;
        return false;
    };

    if (!extractDir.empty()) {
        amiga::GameFiles adf;
        if (!openAdf(adf)) return fail("Disk image not found: " + (adfPath.empty() ? "Supaplex (1991).adf" : adfPath));
        if (!adf.extractTo(extractDir)) return fail(adf.error());
        std::printf("game data extracted to %s\n", extractDir.c_str());
        return 0;
    }

    if (dataDir.empty()) dataDir = (gameDir / "data").string();
    amiga::GameFiles files;
    if (!files.openDir(dataDir)) {
        amiga::GameFiles adf;
        if (!openAdf(adf))
            return fail("Game data not found.\n\nThe folder \"" + dataDir +
                        "\" must contain LEVELS.DAT, INTRO.BIN, MAIN.BIN, GRAPHICS.BIN and HISCORE.BIN.\n"
                        "To create it, put \"Supaplex (1991).adf\" next to the program and start it once.");
        if (!adf.extractTo(dataDir) || !files.openDir(dataDir))
            return fail("Cannot create the data folder \"" + dataDir + "\": " + adf.error());
        std::fprintf(stderr, "game data extracted to %s\n", dataDir.c_str());
    }
    if (savePath.empty()) savePath = (gameDir / "hiscores.sav").string();

    // a new (or reset) hiscore file: clean, or the one from the disk
    bool haveSave = false;
    {
        std::ifstream f(fs::path(savePath), std::ios::binary);
        haveSave = bool(f);
    }
    if (testLevel >= 1 && testLevel <= 111) {
        // a player who may play every level
        std::vector<uint8_t> data = game::cleanHiscores();
        uint8_t* rec = &data[0x280];
        rec[0] = 1;
        std::memcpy(rec + 8, "  EDITOR", 8);
        rec[0x14] = 0;
        rec[0x15] = 111;
        std::memcpy(&data[0x1B8 + 2], "  EDITOR", 8);
        std::ofstream f(fs::path(savePath), std::ios::binary);
        f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    } else if (!haveSave || resetHiscores) {
        std::vector<uint8_t> data = originalHiscores ? files.read("PHIL_03") : game::cleanHiscores();
        std::ofstream f(fs::path(savePath), std::ios::binary);
        f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    }

    amiga::Amiga hw;
    amiga::Paula paula(hw, kSampleRate);
    hw.setPaula(&paula);
    Frontend fe(hw, paula);
    fe.testLevel = (testLevel >= 1 && testLevel <= 111) ? testLevel : 0;
    if (!fe.init(scale, fullscreen)) return 1;
    fe.quitAfter = quitAfter;
    fe.shotPath = shot;
    if (!testInput.empty() && !fe.loadTestInput(testInput)) return fail("cannot read " + testInput);
    hw.setHost(&fe);

    game::Environment env;
    env.files = &files;
    env.savePath = savePath;
    game::attach(hw, env);
    try {
        game::run();
    } catch (const game::QuitGame&) {
    }
    return 0;
}
