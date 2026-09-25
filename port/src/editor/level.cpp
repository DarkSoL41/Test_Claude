#include "level.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace editor {

namespace {

const TileInfo kTiles[T_COUNT] = {
    {"Пусто", "пустая клетка"},
    {"Зонк", "камень: падает и скатывается"},
    {"База", "съедается Murphy"},
    {"Murphy", "игрок (ровно один на уровень)"},
    {"Инфотрон", "собрать нужное число"},
    {"RAM-чип", "стена, разрушается взрывом"},
    {"Железо", "неразрушимая стена"},
    {"Выход", "открывается после сбора инфотронов"},
    {"Оранжевый диск", "падает, взрывается при ударе"},
    {"Порт →", "проход только вправо"},
    {"Порт ↓", "проход только вниз"},
    {"Порт ←", "проход только влево"},
    {"Порт ↑", "проход только вверх"},
    {"Особый порт →", "вправо; меняет гравитацию/заморозки"},
    {"Особый порт ↓", "вниз; меняет гравитацию/заморозки"},
    {"Особый порт ←", "влево; меняет гравитацию/заморозки"},
    {"Особый порт ↑", "вверх; меняет гравитацию/заморозки"},
    {"Сник-снак", "враг, ходит вдоль левой стены"},
    {"Жёлтый диск", "взрывается от терминала"},
    {"Терминал", "взрывает все жёлтые диски"},
    {"Красный диск", "Murphy может заложить"},
    {"Порт ↕", "проход вверх и вниз"},
    {"Порт ↔", "проход влево и вправо"},
    {"Порт ✚", "проход во все стороны"},
    {"Электрон", "враг; при взрыве — 9 инфотронов"},
    {"Баг", "база, которая бьёт током"},
    {"RAM-чип (лев.)", "левая половина чипа"},
    {"RAM-чип (прав.)", "правая половина чипа"},
    {"Железо 1", "вариант оформления стены"},
    {"Железо 2", "вариант оформления стены"},
    {"Железо 3", "вариант оформления стены"},
    {"Железо 4", "вариант оформления стены"},
    {"Железо 5", "вариант оформления стены"},
    {"Железо 6", "вариант оформления стены"},
    {"Железо 7", "вариант оформления стены"},
    {"Железо 8", "вариант оформления стены"},
    {"Железо 9", "вариант оформления стены"},
    {"Железо 10", "вариант оформления стены"},
    {"RAM-чип (верх)", "верхняя половина чипа"},
    {"RAM-чип (низ)", "нижняя половина чипа"},
};

const TileInfo kUnknown = {"Неизвестный код", "игра не знает этот код клетки"};

int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

}  // namespace

const TileInfo& tileInfo(int code) { return code >= 0 && code < T_COUNT ? kTiles[code] : kUnknown; }
bool isHardware(int c) { return c == T_HARDWARE || (c >= T_HW_FIRST && c <= T_HW_LAST); }
bool isSpecialPort(int c) { return c >= T_SPORT_RIGHT && c <= T_SPORT_UP; }

int mirrorTileH(int c) {
    switch (c) {
    case T_PORT_RIGHT: return T_PORT_LEFT;
    case T_PORT_LEFT: return T_PORT_RIGHT;
    case T_SPORT_RIGHT: return T_SPORT_LEFT;
    case T_SPORT_LEFT: return T_SPORT_RIGHT;
    case T_RAM_LEFT: return T_RAM_RIGHT;
    case T_RAM_RIGHT: return T_RAM_LEFT;
    default: return c;
    }
}

int mirrorTileV(int c) {
    switch (c) {
    case T_PORT_UP: return T_PORT_DOWN;
    case T_PORT_DOWN: return T_PORT_UP;
    case T_SPORT_UP: return T_SPORT_DOWN;
    case T_SPORT_DOWN: return T_SPORT_UP;
    case T_RAM_TOP: return T_RAM_BOTTOM;
    case T_RAM_BOTTOM: return T_RAM_TOP;
    default: return c;
    }
}

Level Level::blank() {
    Level l;
    for (int y = 0; y < kMapH; y++)
        for (int x = 0; x < kMapW; x++)
            l.setTile(x, y, (x == 0 || y == 0 || x == kMapW - 1 || y == kMapH - 1) ? T_HARDWARE : T_BASE);
    l.setTile(3, 2, T_MURPHY);
    l.setTile(kMapW - 4, kMapH - 3, T_EXIT);
    l.setTitle("------ NEW LEVEL ------");
    l.normalize();
    return l;
}

std::string Level::title() const { return std::string(reinterpret_cast<const char*>(&b[1446]), kTitleLen); }

void Level::setTitle(const std::string& t) {
    for (int i = 0; i < kTitleLen; i++) {
        char c = i < int(t.size()) ? t[size_t(i)] : ' ';
        if (c >= 'a' && c <= 'z') c = char(c - 32);
        if (uint8_t(c) < 32 || uint8_t(c) > 95) c = ' ';
        b[size_t(1446 + i)] = uint8_t(c);
    }
}

Camera Level::storedCamera() const { return {b[1440] << 8 | b[1441], b[1442] << 8 | b[1443]}; }

void Level::setStoredCamera(Camera c) {
    b[1440] = uint8_t(c.x >> 8); b[1441] = uint8_t(c.x);
    b[1442] = uint8_t(c.y >> 8); b[1443] = uint8_t(c.y);
}

Camera Level::recommendedCamera() const {
    int m = murphyCell();
    if (m < 0) return {0, 0};
    int mx = m % kMapW, my = m / kMapW;
    Camera c;
    c.x = clampi(mx - 10, 0, 39);
    c.y = clampi(my - 6, 0, 11);
    return c;
}

// init_scroll_position ($8D1C)
Camera Level::effectiveCamera(bool* valid) const {
    Camera s = storedCamera();
    int vx = s.x == 40 ? 39 : s.x, vy = s.y == 12 ? 11 : s.y;
    int m = murphyCell();
    bool ok = true;
    if (m < 0) m = 0;
    int mx = m % kMapW, my = m / kMapW;
    if (my < vy) ok = false;  // divu of a negative number: result undefined
    int sy = my - vy, sx = mx - vx;
    auto centreY = [&] { int d = sy - 6; vy += d; sy -= d; };
    auto centreX = [&] { int d = sx - 10; vx += d; sx -= d; };
    if (vy == 0) { if (sy > 6) centreY(); }
    else if (vy == 11) { if (sy < 6) centreY(); }
    else centreY();
    if (vx == 0) { if (sx > 11) centreX(); }
    else if (vx == 39) { if (sx < 10) centreX(); }
    else centreX();
    if (vx < 0 || vx > 39 || vy < 0 || vy > 11) ok = false;
    if (valid) *valid = ok;
    return {vx, vy};
}

std::vector<SpecialPort> Level::specialPorts() const {
    std::vector<SpecialPort> v;
    for (int i = 0; i < kMaxSpecialPorts; i++) {
        const uint8_t* e = &b[size_t(1472 + i * 6)];
        int off = e[0] << 8 | e[1];
        if (off == 0) continue;
        SpecialPort p;
        p.cell = off / 2;
        p.gravity = e[2];
        p.freezeZonks = e[3];
        p.freezeEnemies = e[4];
        v.push_back(p);
    }
    return v;
}

void Level::setSpecialPorts(const std::vector<SpecialPort>& ports) {
    for (int i = 1472; i < 1536; i++) b[size_t(i)] = 0;
    int n = 0;
    for (const SpecialPort& p : ports) {
        if (n >= kMaxSpecialPorts) break;
        uint8_t* e = &b[size_t(1472 + n * 6)];
        int off = p.cell * 2;
        e[0] = uint8_t(off >> 8); e[1] = uint8_t(off);
        e[2] = p.gravity; e[3] = p.freezeZonks; e[4] = p.freezeEnemies; e[5] = 0;
        n++;
    }
    b[1471] = uint8_t(n);
}

int Level::findPort(int cell) const {
    auto v = specialPorts();
    for (size_t i = 0; i < v.size(); i++)
        if (v[i].cell == cell) return int(i);
    return -1;
}

int Level::count(int code) const {
    int n = 0;
    for (int i = 0; i < kMapW * kMapH; i++) n += b[size_t(i)] == code;
    return n;
}

int Level::murphyCell() const {
    for (int i = 0; i < kMapW * kMapH; i++)
        if (b[size_t(i)] == T_MURPHY) return i;
    return -1;
}

void Level::normalize() {
    // keep the camera the game would show; recentre only if it leaves the map
    bool valid = false;
    Camera eff = effectiveCamera(&valid);
    setStoredCamera(valid ? eff : recommendedCamera());
    std::vector<SpecialPort> keep;
    for (const SpecialPort& p : specialPorts())
        if (p.cell < kMapW * kMapH && isSpecialPort(b[size_t(p.cell)]) &&
            std::none_of(keep.begin(), keep.end(), [&](const SpecialPort& k) { return k.cell == p.cell; }))
            keep.push_back(p);
    setSpecialPorts(keep);
    b[1445] = 0;
}

std::vector<Issue> Level::validate() const {
    std::vector<Issue> out;
    auto add = [&](Issue::Level lv, std::string t, int cell = -1) {
        out.push_back({lv, std::move(t), cell < 0 ? -1 : cell % kMapW, cell < 0 ? -1 : cell / kMapW});
    };
    char buf[256];
    int murphys = count(T_MURPHY);
    if (murphys == 0) add(Issue::Error, "Нет Murphy — уровень не запустится");
    if (murphys > 1) {
        int second = -1, n = 0;
        for (int i = 0; i < kMapW * kMapH; i++)
            if (b[size_t(i)] == T_MURPHY && ++n == 2) { second = i; break; }
        std::snprintf(buf, sizeof buf, "Murphy на уровне %d раз: игра управляет первым, остальные — неподвижные «двойники»", murphys);
        add(Issue::Warning, buf, second);
    }
    if (count(T_EXIT) == 0) add(Issue::Warning, "Нет выхода — уровень нельзя пройти");

    int infotrons = count(T_INFOTRON), electrons = count(T_ELECTRON);
    int need = infotronsNeeded() ? infotronsNeeded() : infotrons;
    if (infotronsNeeded() > infotrons + 9 * electrons) {
        std::snprintf(buf, sizeof buf, "Нужно %d инфотронов, а на уровне %d (и %d электронов × 9)", infotronsNeeded(),
                      infotrons, electrons);
        add(Issue::Error, buf);
    } else if (infotronsNeeded() > infotrons) {
        std::snprintf(buf, sizeof buf, "Нужно %d инфотронов, на уровне только %d — остальные придётся добыть из электронов",
                      infotronsNeeded(), infotrons);
        add(Issue::Warning, buf);
    }
    (void)need;

    // border
    int holes = 0, firstHole = -1;
    for (int y = 0; y < kMapH; y++)
        for (int x = 0; x < kMapW; x++) {
            if (x != 0 && y != 0 && x != kMapW - 1 && y != kMapH - 1) continue;
            if (!isHardware(tile(x, y))) { if (holes++ == 0) firstHole = y * kMapW + x; }
        }
    if (holes) {
        std::snprintf(buf, sizeof buf,
                      "Рамка не из «железа» в %d клетках: Murphy через край не пройдёт, но зонк или враг, "
                      "ушедший за карту, вешает игру на Amiga", holes);
        add(Issue::Error, buf, firstHole);
    }

    // special ports
    int sports = 0, missing = -1;
    for (int i = 0; i < kMapW * kMapH; i++)
        if (isSpecialPort(b[size_t(i)])) {
            sports++;
            if (findPort(i) < 0 && missing < 0) missing = i;
        }
    if (sports > kMaxSpecialPorts) {
        std::snprintf(buf, sizeof buf, "Особых портов %d, а в формате уровня помещается %d", sports, kMaxSpecialPorts);
        add(Issue::Error, buf);
    }
    if (missing >= 0) add(Issue::Warning, "Особый порт без записи: работает как обычный порт", missing);
    for (const SpecialPort& p : specialPorts()) {
        if (p.cell >= kMapW * kMapH || !isSpecialPort(b[size_t(p.cell)])) {
            add(Issue::Info, "Запись особого порта не на особом порту (мусор) — уберётся при сохранении",
                p.cell < kMapW * kMapH ? p.cell : -1);
            break;
        }
    }
    if (b[1532] || b[1533] || b[1534] || b[1535])
        add(Issue::Info, "Байты 1532–1535 не нулевые (игра читает их как 11-й порт) — обнулятся при сохранении");

    bool valid = true;
    Camera eff = effectiveCamera(&valid);
    if (!valid) {
        std::snprintf(buf, sizeof buf, "Стартовая камера уходит за карту (%d, %d) — будет пересчитана при сохранении", eff.x,
                      eff.y);
        add(Issue::Warning, buf);
    }

    std::string t = title();
    for (char c : t)
        if (uint8_t(c) < 32 || uint8_t(c) > 95) { add(Issue::Warning, "В названии есть символы, которых нет в шрифте игры"); break; }
    for (int i = 0; i < kMapW * kMapH; i++)
        if (b[size_t(i)] >= T_COUNT) { add(Issue::Error, "Неизвестный код клетки", i); break; }
    return out;
}

bool LevelSet::readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool LevelSet::writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    return bool(f);
}

bool LevelSet::load(const std::string& path, std::string* err) {
    std::vector<uint8_t> d;
    if (!readFile(path, d)) { if (err) *err = "не удалось прочитать " + path; return false; }
    if (d.size() != size_t(kLevelCount * kLevelSize)) {
        if (err) *err = path + ": ожидалось 111 уровней по 1536 байт";
        return false;
    }
    for (int i = 0; i < kLevelCount; i++)
        std::copy_n(d.begin() + std::ptrdiff_t(i * kLevelSize), kLevelSize, levels[size_t(i)].b.begin());
    edited.fill(false);
    return true;
}

bool LevelSet::save(const std::string& path, std::string* err) {
    std::vector<uint8_t> d;
    d.reserve(size_t(kLevelCount * kLevelSize));
    for (int i = 0; i < kLevelCount; i++) {
        if (edited[size_t(i)]) levels[size_t(i)].normalize();
        d.insert(d.end(), levels[size_t(i)].b.begin(), levels[size_t(i)].b.end());
    }
    // write to a temporary file first, then replace
    std::string tmp = path + ".tmp";
    if (!writeFile(tmp, d)) { if (err) *err = "не удалось записать " + tmp; return false; }
    std::error_code ec;
    std::filesystem::rename(std::filesystem::u8path(tmp), std::filesystem::u8path(path), ec);
    if (ec) {
        std::filesystem::remove(std::filesystem::u8path(path), ec);
        std::filesystem::rename(std::filesystem::u8path(tmp), std::filesystem::u8path(path), ec);
        if (ec) { if (err) *err = "не удалось заменить " + path; return false; }
    }
    return true;
}

}  // namespace editor
