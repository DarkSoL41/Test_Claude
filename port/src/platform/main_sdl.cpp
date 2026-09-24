// Supaplex (Amiga) — Windows/Linux front end on SDL2.
//
// Runs the translated game on the emulated Amiga machine. Every time the
// virtual beam completes a frame, this front end presents the picture, feeds
// the audio produced by Paula to SDL, reads the input devices and waits for
// the next PAL frame (50 Hz).
//
// Controls
//   joystick : cursor keys / numeric keypad, fire = Space, Ctrl, Alt or Enter;
//              any game controller (d-pad / left stick, A B X Y = fire)
//   mouse    : menu pointer, left / right button
//   keyboard : typed keys go to the Amiga keyboard (player names)
//   F11 or Alt+Enter : fullscreen,  Pause : pause,  F12 : quit
#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "amiga/adf.hpp"
#include "amiga/amiga.hpp"
#include "amiga/paula.hpp"
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
                                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
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
    std::string shotPath;         // debugging: write the last frame here (PPM)

    void onFrame() override {
        if (quitAfter >= 0 && long(hw_.frameCount()) >= quitAfter) {
            if (!shotPath.empty()) writeShot();
            throw game::QuitGame{};
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

    void updateJoystick() {
        const Uint8* k = SDL_GetKeyboardState(nullptr);
        bool up = k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_KP_8];
        bool down = k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_KP_2] || k[SDL_SCANCODE_KP_5];
        bool left = k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_KP_4];
        bool right = k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_KP_6];
        bool fire = k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL] ||
                    k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_KP_0] || k[SDL_SCANCODE_RETURN];
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
        hw_.setJoystick(up, down, left, right, fire);
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
                if (down && sc == SDL_SCANCODE_PAUSE) { paused_ = !paused_; break; }
                if (e.key.repeat) break;
                int code = amigaKey(sc);
                if (code >= 0) hw_.keyEvent(uint8_t(code), down);
                break;
            }
            case SDL_MOUSEMOTION: {
                // Amiga mouse counts; the game uses half of them as lowres pixels
                double sx = dst_.w > 0 ? 640.0 / dst_.w : 1.0;
                double sy = dst_.h > 0 ? 512.0 / dst_.h : 1.0;
                accX_ += e.motion.xrel * sx;
                accY_ += e.motion.yrel * sy;
                int dx = int(accX_), dy = int(accY_);
                accX_ -= dx;
                accY_ -= dy;
                hw_.moveMouse(dx, dy);
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                bool d = e.type == SDL_MOUSEBUTTONDOWN;
                if (e.button.button == SDL_BUTTON_LEFT) lmb_ = d;
                if (e.button.button == SDL_BUTTON_RIGHT) rmb_ = d;
                hw_.setMouseButtons(lmb_, rmb_);
                if (d && !SDL_GetRelativeMouseMode()) SDL_SetRelativeMouseMode(SDL_TRUE);
                break;
            }
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) SDL_SetRelativeMouseMode(SDL_TRUE);
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) SDL_SetRelativeMouseMode(SDL_FALSE);
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
        updateJoystick();
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
    double accX_ = 0, accY_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    std::string adfPath, savePath;
    int scale = 3;
    bool fullscreen = false;
    long quitAfter = -1;
    std::string shot;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--adf") adfPath = next();
        else if (a == "--save") savePath = next();
        else if (a == "--scale") scale = std::max(1, std::atoi(next().c_str()));
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--frames") quitAfter = std::atol(next().c_str());
        else if (a == "--shot") shot = next();
        else if (a == "--help" || a == "-h") {
            std::printf("usage: supaplex [--adf image.adf] [--save hiscores.sav] [--scale N] [--fullscreen]\n");
            return 0;
        } else if (adfPath.empty()) adfPath = a;
    }
    char* base = SDL_GetBasePath();
    std::string exeDir = base ? base : "";
    if (base) SDL_free(base);
    amiga::Adf adf;
    const char* candidates[] = {"Supaplex (1991).adf", "Supaplex.adf", "supaplex.adf"};
    bool ok = false;
    if (!adfPath.empty()) ok = adf.open(adfPath);
    for (const char* c : candidates) {
        if (ok || !adfPath.empty()) break;
        ok = adf.open(exeDir + c) || adf.open(c) || adf.open(std::string("../") + c);
    }
    if (!ok) {
        std::string msg = "Supaplex disk image not found.\n\nPut \"Supaplex (1991).adf\" next to the program "
                          "or pass its path: supaplex --adf <file>.";
        if (!adf.error().empty()) msg += "\n\n" + adf.error();
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Supaplex", msg.c_str(), nullptr);
        std::fprintf(stderr, "%s\n", msg.c_str());
        return 1;
    }
    if (savePath.empty()) {
        char* pref = SDL_GetPrefPath("Supaplex", "SupaplexAmiga");
        savePath = std::string(pref ? pref : exeDir.c_str()) + "hiscores.sav";
        if (pref) SDL_free(pref);
    }

    amiga::Amiga hw;
    amiga::Paula paula(hw, kSampleRate);
    hw.setPaula(&paula);
    Frontend fe(hw, paula);
    if (!fe.init(scale, fullscreen)) return 1;
    fe.quitAfter = quitAfter;
    fe.shotPath = shot;
    hw.setHost(&fe);

    game::Environment env;
    env.adf = &adf;
    env.savePath = savePath;
    game::attach(hw, env);
    try {
        game::run();
    } catch (const game::QuitGame&) {
    }
    return 0;
}
