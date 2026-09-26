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
//   keyboard        : in a level, cursor keys + Space act as the joystick (a
//                     PC has no joystick port) and Esc gives up (the left
//                     button, as in the PC version); Space / Enter / Esc go on
//                     from the screens that wait for a click; any key or click
//                     skips the intro; while the game asks for a player name
//                     the keys go to the Amiga keyboard. In the main menu
//                     Space starts the level (the fire button, as in the PC
//                     version); a Space held from another screen does not.
//   F11 or Alt+Enter : fullscreen,  Pause : pause,  F12 or window close : quit
//
// Smooth picture: the game draws 50 frames a second (PAL) and moves the
// picture evenly, 2 pixels a frame. A 60 Hz monitor cannot show 50 frames
// evenly (every fifth one stays twice as long: judder). So the picture is
// shown with vertical sync, and (supaplex.ini, see writeDefaultIni):
//   * in fullscreen the monitor is switched to a 50 Hz (or 100 Hz) mode if it
//     has one: perfectly smooth at the original speed;
//   * speed=display runs one game frame per monitor refresh (or per 2, 3 ...
//     refreshes, whatever is closest to 50 a second): smooth everywhere, but
//     the game then runs at that rate (60 Hz: 20% faster), like a PAL game on
//     an NTSC Amiga. The sound keeps its pitch. The game logic is the same.
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
#include "icon_data.hpp"

namespace {

constexpr int kSampleRate = 48000;
// PAL frame: 313 lines of 227.5 colour clocks at 3.546895 MHz
constexpr double kFrameSeconds = 313.0 * 227.5 / 3546895.0;
constexpr uint32_t kMenuCopper = 0x1B896;  // copper list of the main menu (main_menu, $99C0)

// Settings from supaplex.ini (next to the program) and the command line.
struct Settings {
    int scale = 3;
    bool fullscreen = false;
    bool vsync = true;          // show frames on the monitor's vertical blank
    bool fullscreen50 = true;   // fullscreen: switch the monitor to 50 / 100 Hz if possible
    bool speedDisplay = false;  // speed=display: one game frame per refresh (or per n refreshes)
};

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

    bool init(const Settings& set, bool headless) {
        set_ = set;
        headless_ = headless;
        int scale = set.scale;
        bool fullscreen = set.fullscreen;
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
            std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return false;
        }
        int w = 320 * scale, h = 256 * scale;
        window_ = SDL_CreateWindow("Supaplex (Amiga)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (testLevel ? SDL_WINDOW_HIDDEN : 0));
        if (!window_) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
        if (SDL_Surface* icon = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<uint32_t*>(kIcon_supaplex), 64, 64, 32, 64 * 4,
                                                                   SDL_PIXELFORMAT_ARGB8888)) {
            SDL_SetWindowIcon(window_, icon);
            SDL_FreeSurface(icon);
        }
        bool vsync = set.vsync && !headless;
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
        if (!renderer_ && vsync) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer_) { std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return false; }
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                     amiga::kOutWidth, amiga::kOutHeight);
        SDL_RendererInfo ri{};
        vsync_ = vsync && SDL_GetRendererInfo(renderer_, &ri) == 0 && (ri.flags & SDL_RENDERER_PRESENTVSYNC);
        if (fullscreen) setFullscreen(true);
        else configureTiming();

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

    // The game read or wrote the disk. On the Amiga that took a second or
    // more, and a click that started it (OK on "skip level", the click that
    // leaves the results) was over long before the next screen looked at the
    // buttons. Here it is instant, so buttons held now reach the game again
    // only after they are released: otherwise the same click also presses
    // whatever lies under the pointer on the next screen.
    // A level or the demo can start and look at the buttons within one
    // frame (the demo level comes from memory, not from the disk): as soon
    // as game_frame has started counting, buttons held since before the
    // level count as released until they are let go. Called before every
    // read of the button registers, so it is in time whatever the timing.
    void onButtonRead() override {
        if (inLevel_ || (testLevel && testState_ < 3)) return;
        if (hw_.chip()[0x11294] == lastTick_) return;
        holdLmb_ = lmb_;
        holdRmb_ = rmb_;
        holdFire_ = true;
        hw_.setMouseButtons(false, false);
        updateJoystick();
    }

    void diskAccess() {
        holdLmb_ = lmb_;
        holdRmb_ = rmb_;
        holdFire_ = true;
        hw_.setMouseButtons(false, false);
        updateJoystick();
    }

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
        if (skipIntro_ && !menuSeen_) {
            // a key or a click during the intro: run it at full speed, without
            // picture and sound, up to the main menu (the game does the same
            // things, only nobody waits for it)
            if (hw_.copperList() == kMenuCopper) {
                menuSeen_ = true;
                skipIntro_ = false;
                holdLmb_ = holdRmb_ = holdFire_ = true;  // the key or click that skipped stays out of the menu
                nextFrame_ = SDL_GetPerformanceCounter();
            } else {
                if (++skipFrames_ % 25 == 0) {
                    SDL_Event e;
                    while (SDL_PollEvent(&e))
                        if (e.type == SDL_QUIT) throw game::QuitGame{};
                }
                paula_.samples().clear();
                nextFrame_ = SDL_GetPerformanceCounter();
                return;
            }
        }
        handleEvents();
        if (!menuSeen_ && hw_.copperList() == kMenuCopper) menuSeen_ = true;
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
        const uint32_t* fb = shownPicture();
        for (int i = 0; i < amiga::kOutWidth * amiga::kOutHeight; i++) {
            unsigned char px[3] = {uint8_t(fb[i] >> 16), uint8_t(fb[i] >> 8), uint8_t(fb[i])};
            std::fwrite(px, 1, 3, f);
        }
        std::fclose(f);
    }

    // Fullscreen: with fullscreen50 the monitor goes to a 50 Hz (or 100 Hz)
    // mode of the desktop size if it has one, else the desktop is used as is.
    void setFullscreen(bool on) {
        fullscreen_ = on;
        if (!on) {
            SDL_SetWindowFullscreen(window_, 0);
        } else {
            bool done = false;
            if (set_.fullscreen50 && vsync_) {
                int d = SDL_GetWindowDisplayIndex(window_);
                SDL_DisplayMode desk{}, best{}, m{};
                SDL_GetDesktopDisplayMode(d, &desk);
                int bestScore = -1;
                for (int i = 0; i < SDL_GetNumDisplayModes(d); i++) {
                    if (SDL_GetDisplayMode(d, i, &m) != 0) continue;
                    if (m.refresh_rate != 50 && m.refresh_rate != 100) continue;
                    // the desktop size first, then the biggest; 50 Hz before 100 Hz
                    int score = (m.w == desk.w && m.h == desk.h ? 1 << 30 : 0) + m.w * m.h / 16 + (m.refresh_rate == 50 ? 1 : 0);
                    if (score > bestScore) { bestScore = score; best = m; }
                }
                if (bestScore >= 0 && SDL_SetWindowDisplayMode(window_, &best) == 0 &&
                    SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN) == 0)
                    done = true;
            }
            if (!done) SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
        configureTiming();
    }

    // How frames reach the monitor. With vertical sync and a refresh of 50 or
    // 100 Hz (or with speed=display) the game is driven by the refresh: n
    // refreshes per game frame. Otherwise a 50 Hz clock drives it.
    void configureTiming() {
        lockToDisplay_ = false;
        presentsPerFrame_ = 1;
        gameRate_ = 1.0 / kFrameSeconds;
        if (!vsync_ || headless_) return;
        SDL_DisplayMode m{};
        if (SDL_GetWindowDisplayMode(window_, &m) != 0 || m.refresh_rate <= 0) return;
        if (!fullscreen_ || SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN_DESKTOP) {
            SDL_DisplayMode cur{};
            if (SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window_), &cur) == 0 && cur.refresh_rate > 0) m = cur;
        }
        int n = std::max(1, int(m.refresh_rate / 50.0 + 0.5));
        double rate = double(m.refresh_rate) / n;
        if (std::abs(rate - 50.0) < 0.6 || set_.speedDisplay) {
            lockToDisplay_ = true;
            presentsPerFrame_ = n;
            gameRate_ = std::abs(rate - 50.0) < 0.6 ? 1.0 / kFrameSeconds : rate;
        }
        std::fprintf(stderr, "display %d Hz: %s, %.2f game frames a second\n", m.refresh_rate,
                     lockToDisplay_ ? "frames on the vertical blank" : "50 Hz clock", gameRate_);
    }

    // ---- the port's credit line on the game's credits screen ------------
    // (the red button right of the message line): drawn with the game's own
    // font into the picture that is shown, the game's memory is not touched

    // font of draw_text ($B2A0): 8x7 glyphs, one bit plane, $3A bytes a row
    // at the address in $1B5D8; the glyph table ($1B52A) pairs a character
    // with its column, $FF ends it
    int glyphColumn(char ch) const {
        for (uint32_t a = 0x1B52A; a < 0x1B5A0; a += 2) {
            uint8_t c = hw_.chip()[a];
            if (c == 0xFF) return -1;
            if (c == uint8_t(ch)) return hw_.chip()[a + 1];
        }
        return -1;
    }
    const uint8_t* fontRow(int column, int row) const {
        return &hw_.chip()[(hw_.chipL(0x1B5D8) + uint32_t(column) + uint32_t(row) * 0x3A) & (amiga::kChipSize - 1)];
    }
    // the credits screen: its copper list ($1BB7E, shared with the statistics)
    // and its first line of text in the bit plane (menu_info_screen, $A3BA)
    bool creditsShown() const {
        if (hw_.copperList() != 0x1BB7E) return false;
        const char* first = "SUPAPLEX";  // "SUPAPLEX BY THINK!WARE ..." at x 180, y 16
        uint32_t line = hw_.chipL(0x1B5E8) + 16 * 80 + 180 / 8;
        for (int i = 0; first[i]; i++) {
            int col = glyphColumn(first[i]);
            if (col < 0) return false;
            for (int r = 0; r < 7; r++)
                if (hw_.chip()[(line + uint32_t(i + r * 80)) & (amiga::kChipSize - 1)] != *fontRow(col, r)) return false;
        }
        return true;
    }
    void drawCreditText(std::vector<uint32_t>& fb, int y, const char* text, uint32_t colour) const {
        int x0 = (amiga::kOutWidth - int(std::strlen(text)) * 8) / 2;
        for (int i = 0; text[i]; i++) {
            int col = glyphColumn(text[i]);
            if (col < 0) continue;
            for (int r = 0; r < 7; r++) {
                uint8_t bits = *fontRow(col, r);
                for (int b = 0; b < 8; b++)
                    if (bits & (0x80 >> b)) {
                        int px = x0 + i * 8 + b, py = y + r;
                        if (px >= 0 && px < amiga::kOutWidth && py >= 0 && py < amiga::kOutHeight)
                            fb[size_t(py * amiga::kOutWidth + px)] = colour;
                    }
            }
        }
    }

    // the picture as it is shown: the emulated display plus the credit line
    const uint32_t* shownPicture() {
        const uint32_t* picture = hw_.frameBuffer();
        if (!creditsShown()) return picture;
        credits_.assign(picture, picture + amiga::kOutWidth * amiga::kOutHeight);
        drawCreditText(credits_, 234, "PORTED TO WINDOWS BY DARKSOL (AKA KUFTERIN) - DISCORD: DARKSOL41", 0xFFFFFFFF);
        drawCreditText(credits_, 244, "WITH THE HELP AND SUPPORT OF CLAUDE AI", 0xFFFFFFFF);
        return credits_.data();
    }

    void present() {
        const uint32_t* picture = shownPicture();
        SDL_UpdateTexture(texture_, nullptr, picture, amiga::kOutWidth * 4);
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
        for (int i = 1; lockToDisplay_ && i < presentsPerFrame_; i++) {  // hold the frame for n refreshes
            SDL_RenderClear(renderer_);
            SDL_RenderCopy(renderer_, texture_, nullptr, &dst_);
            SDL_RenderPresent(renderer_);
        }
    }

    void queueAudio() {
        auto& s = paula_.samples();
        // Paula makes 48000 samples per emulated second; when the game runs
        // faster or slower than 50 frames a second, resample so that the
        // pitch stays and the tempo follows the game
        double step = gameRate_ * kFrameSeconds;  // input samples per output sample
        if (std::abs(step - 1.0) > 0.001 && s.size() >= 2) {
            resampled_.clear();
            size_t frames = s.size() / 2;
            while (resamplePos_ < double(frames)) {
                size_t i = size_t(resamplePos_);
                double f = resamplePos_ - double(i);
                for (int c = 0; c < 2; c++) {
                    int a = i == 0 ? resampleLast_[c] : s[(i - 1) * 2 + size_t(c)];
                    int b = s[i * 2 + size_t(c)];
                    resampled_.push_back(int16_t(a + (b - a) * f));
                }
                resamplePos_ += step;
            }
            resamplePos_ -= double(frames);
            resampleLast_[0] = s[(frames - 1) * 2];
            resampleLast_[1] = s[(frames - 1) * 2 + 1];
            s.swap(resampled_);
        }
        if (audio_ && !s.empty()) {
            const Uint32 maxQueued = Uint32(kSampleRate * 4 * 0.15);  // keep latency below 150 ms
            if (SDL_GetQueuedAudioSize(audio_) > maxQueued) SDL_ClearQueuedAudio(audio_);
            SDL_QueueAudio(audio_, s.data(), Uint32(s.size() * sizeof(int16_t)));
        }
        s.clear();
    }

    void pace() {
        const Uint64 freq = SDL_GetPerformanceFrequency();
        if (lockToDisplay_) {
            // the vertical blank paces; the clock only guards against a
            // driver that ignores vertical sync (then at most gameRate_)
            Uint64 now = SDL_GetPerformanceCounter();
            Uint64 minGap = Uint64(0.9 / gameRate_ * double(freq));
            while (now < lastPresent_ + minGap) {
                SDL_Delay(1);
                now = SDL_GetPerformanceCounter();
            }
            lastPresent_ = now;
            nextFrame_ = now;
            return;
        }
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
        if (!inMenu()) menuSpace_ = false;
        if (menuSpace_) fire = true;
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
                    setFullscreen(!fullscreen_);
                    break;
                }
                if (down && sc == SDL_SCANCODE_F12) throw game::QuitGame{};
                if (down && sc == SDL_SCANCODE_ESCAPE && testLevel) throw game::QuitGame{};  // back to the editor
                if (down && sc == SDL_SCANCODE_PAUSE) { paused_ = !paused_; break; }
                if (e.key.repeat) break;
                if (down && !menuSeen_ && !testLevel) skipIntro_ = true;
                typeKey(sc, down);
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                bool d = e.type == SDL_MOUSEBUTTONDOWN;
                if (e.button.button == SDL_BUTTON_LEFT) mouseLmb_ = d;
                if (e.button.button == SDL_BUTTON_RIGHT) rmb_ = d;
                if (d && !menuSeen_ && !testLevel) skipIntro_ = true;
                break;
            }
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) mouseLmb_ = rmb_ = false;
                if (e.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED || e.window.event == SDL_WINDOWEVENT_MOVED) configureTiming();
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
        lmb_ = mouseLmb_ || keyClick();
        trackLevel();
        if (testLevel) driveTest();
        updateJoystick();
        updateMouse();
    }

    // Keys that act as the left mouse button, like in the PC version: in a
    // level Esc gives up (the left button there; Space stays the joystick's
    // fire), on the screens that wait for a click (results, credits,
    // statistics, gfx tutor, ...) Space, Enter and Esc go on. Not in the main
    // menu (a click there presses what is under the pointer) and not while a
    // name is typed in (the keys are the Amiga keyboard then).
    bool keyClick() const {
        if (nameEntry()) return false;
        bool esc = keyDown(SDL_SCANCODE_ESCAPE) && !testLevel;  // the editor's test: Esc goes back
        if (inLevel_) return esc;
        if (!menuSeen_ || hw_.copperList() == kMenuCopper) return false;
        const Uint8* ks = testMode_ ? nullptr : SDL_GetKeyboardState(nullptr);
        bool alt = ks && (ks[SDL_SCANCODE_LALT] || ks[SDL_SCANCODE_RALT]);  // Alt+Enter: fullscreen
        return esc || keyDown(SDL_SCANCODE_SPACE) ||
               (!alt && (keyDown(SDL_SCANCODE_RETURN) || keyDown(SDL_SCANCODE_KP_ENTER)));
    }

    // Space in the main menu starts the level, like the fire button (as in
    // the PC version). Only a press that begins while the menu is shown
    // counts: Space still held from the results or from a level does not.
    bool inMenu() const { return menuSeen_ && !inLevel_ && hw_.copperList() == kMenuCopper && !nameEntry(); }
    void noteMenuSpace(SDL_Scancode sc, bool down) {
        if (sc != SDL_SCANCODE_SPACE) return;
        menuSpace_ = down && inMenu();
    }

    void typeKey(SDL_Scancode sc, bool down) {
        noteMenuSpace(sc, down);
        int code = amigaKey(sc);
        if (code < 0) return;
        if (down) {
            // keys pressed outside the name entry would stay queued in the CIA
            // and pop up in the next name entry
            if (!nameEntry() || keySent_[code]) return;
            keySent_[code] = true;
        } else {
            // The release of a key the game has seen pressed always goes out,
            // like from the real keyboard. Return ends the name entry before it
            // is released: the release waits in the CIA until the next entry
            // enables the keyboard interrupt. Without it the game's key table
            // keeps Return down and the next name entry never sees a new key.
            if (!keySent_[code]) return;
            keySent_[code] = false;
        }
        hw_.keyEvent(uint8_t(code), down);
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
            else if (ev.what == "lmb") mouseLmb_ = ev.a != 0;
            if ((ev.what == "lmb" || ev.what == "key") && ev.a && !menuSeen_ && !testLevel) skipIntro_ = true;
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
    Uint64 nextFrame_ = 0, lastPresent_ = 0;
    std::vector<uint32_t> credits_;  // the picture with the port's credit line
    Settings set_;
    bool headless_ = false, vsync_ = false, lockToDisplay_ = false;
    int presentsPerFrame_ = 1;
    double gameRate_ = 1.0 / kFrameSeconds;  // game frames per real second
    std::vector<int16_t> resampled_;
    double resamplePos_ = 0;
    int resampleLast_[2] = {};
    bool fullscreen_ = false, paused_ = false, lmb_ = false, rmb_ = false;
    bool mouseLmb_ = false;                      // the left mouse button itself (lmb_ also counts keyClick)
    bool menuSeen_ = false, skipIntro_ = false;  // the intro is over / is being skipped
    long skipFrames_ = 0;
    bool menuSpace_ = false;  // Space pressed in the main menu (acts as fire there)
    bool inLevel_ = false, holdLmb_ = false, holdRmb_ = false, holdFire_ = false;
    uint8_t lastTick_ = 0;
    bool keySent_[128] = {};  // Amiga keys whose press went to the game
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

// supaplex.ini next to the program: written with the defaults on the first
// start, so that the options can be found
void writeDefaultIni(const std::filesystem::path& p) {
    std::ofstream f(p);
    f << "; Supaplex (Amiga) settings\n"
         "; window size: the Amiga picture times this\n"
         "scale=3\n"
         "; start in fullscreen (F11 or Alt+Enter switch)\n"
         "fullscreen=0\n"
         "; show frames on the monitor's vertical blank (no tearing)\n"
         "vsync=1\n"
         "; fullscreen: switch the monitor to 50 Hz (or 100 Hz) if it can:\n"
         "; the game's 50 frames a second then run perfectly smooth\n"
         "fullscreen_50hz=1\n"
         "; speed: amiga = 50 frames a second like a PAL Amiga (on a 60 Hz monitor\n"
         ";   the picture judders a little, every fifth frame is shown twice);\n"
         ";   display = one game frame per monitor refresh (per 2, 3 ... refreshes\n"
         ";   on 100+ Hz monitors): perfectly smooth, but the game runs at that\n"
         ";   rate (60 Hz: 20% faster). The game logic is the same either way.\n"
         "speed=amiga\n";
}

void readIni(const std::filesystem::path& p, Settings& s) {
    std::ifstream f(p);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
        int n = std::atoi(v.c_str());
        if (k == "scale") s.scale = std::clamp(n, 1, 10);
        else if (k == "fullscreen") s.fullscreen = n != 0;
        else if (k == "vsync") s.vsync = n != 0;
        else if (k == "fullscreen_50hz") s.fullscreen50 = n != 0;
        else if (k == "speed") s.speedDisplay = v == "display";
    }
}

int main(int argc, char** argv) {
    std::string adfPath, savePath, dataDir, extractDir;
    int scale = 0;
    bool fullscreen = false, originalHiscores = false, resetHiscores = false;
    bool noVsync = false, speedDisplay = false;
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
        else if (a == "--no-vsync") noVsync = true;
        else if (a == "--speed-display") speedDisplay = true;
        else if (a == "--original-hiscores") originalHiscores = true;
        else if (a == "--reset-hiscores") resetHiscores = true;
        else if (a == "--frames") quitAfter = std::atol(next().c_str());
        else if (a == "--shot") shot = next();
        else if (a == "--test-input") testInput = next();
        else if (a == "--test-level") testLevel = std::atoi(next().c_str());
        else if (a == "--help" || a == "-h") {
            std::printf(
                "usage: supaplex [--scale N] [--fullscreen] [--no-vsync] [--speed-display]\n"
                "                [--reset-hiscores] [--original-hiscores]\n"
                "                [--data DIR] [--save FILE] [--adf IMAGE] [--extract DIR]\n"
                "Everything lives in the program's folder: the game data in data\\ (LEVELS.DAT,\n"
                "INTRO.BIN, MAIN.BIN, GRAPHICS.BIN, HISCORE.BIN) and the hiscores in hiscores.sav.\n"
                "If data\\ is missing, it is extracted once from \"Supaplex (1991).adf\" found next\n"
                "to the program (or given with --adf); --extract DIR only extracts and exits.\n"
                "A new hiscore file starts without players and records; --original-hiscores\n"
                "starts it with the disk's CRYSTAL records and players; --reset-hiscores starts over.\n"
                "Display options: supaplex.ini next to the program (written on the first start).\n");
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
    bool headless = quitAfter >= 0 || fe.testLevel || !testInput.empty();
    Settings settings;
    const fs::path ini = gameDir / "supaplex.ini";
    if (!headless && !fs::exists(ini)) writeDefaultIni(ini);
    readIni(ini, settings);
    if (scale > 0) settings.scale = scale;
    if (fullscreen) settings.fullscreen = true;
    if (noVsync) settings.vsync = false;
    if (speedDisplay) settings.speedDisplay = true;
    if (headless) settings.fullscreen = false;
    if (!fe.init(settings, headless)) return 1;
    fe.quitAfter = quitAfter;
    fe.shotPath = shot;
    if (!testInput.empty() && !fe.loadTestInput(testInput)) return fail("cannot read " + testInput);
    hw.setHost(&fe);

    game::Environment env;
    env.files = &files;
    env.savePath = savePath;
    env.onDiskAccess = [&fe] { fe.diskAccess(); };
    game::attach(hw, env);
    try {
        game::run();
    } catch (const game::QuitGame&) {
    }
    return 0;
}
