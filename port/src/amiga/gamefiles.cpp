#include "gamefiles.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace amiga {

namespace {

struct Entry { const char* disk; const char* file; };
const Entry kFiles[] = {
    {"PHIL_00", "INTRO.BIN"},
    {"PHIL_01", "MAIN.BIN"},
    {"PHIL_02", "GRAPHICS.BIN"},
    {"PHIL_03", "HISCORE.BIN"},
};
const char* const kLevelsFile = "LEVELS.DAT";

bool readFile(const std::filesystem::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool writeFile(const std::filesystem::path& p, const std::vector<uint8_t>& data) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    return bool(f);
}

}  // namespace

int GameFiles::levelNumber(const std::string& name) {
    if (name.size() != 7 || name.compare(0, 5, "PHIL_") != 0) return 0;
    unsigned n = 0;
    if (std::sscanf(name.c_str() + 5, "%2x", &n) != 1) return 0;
    int level = int(n) - 0x0F;  // level 1 = PHIL_10
    return (level >= 1 && level <= kLevelCount) ? level : 0;
}

bool GameFiles::openDir(const std::string& dir) {
    namespace fs = std::filesystem;
    files_.clear();
    levels_.clear();
    fromAdf_ = false;
    for (const Entry& e : kFiles) {
        std::vector<uint8_t> data;
        if (!readFile(fs::path(dir) / e.file, data) || data.empty()) {
            err_ = "missing " + (fs::path(dir) / e.file).string();
            return false;
        }
        files_[e.disk] = std::move(data);
    }
    if (!readFile(fs::path(dir) / kLevelsFile, levels_) || levels_.size() != kLevelCount * kLevelSize) {
        err_ = "missing or damaged " + (fs::path(dir) / kLevelsFile).string() + " (expected 111 levels of 1536 bytes)";
        levels_.clear();
        return false;
    }
    return true;
}

bool GameFiles::openAdf(const std::string& path) {
    files_.clear();
    levels_.clear();
    fromAdf_ = adf_.open(path);
    if (!fromAdf_) err_ = adf_.error();
    return fromAdf_;
}

std::vector<uint8_t> GameFiles::read(const std::string& name) const {
    if (fromAdf_) return adf_.read(name);
    if (int level = levelNumber(name)) {
        // the game loads the whole 2048-byte file and uses its first 1536 bytes;
        // the unused rest of the file comes back as zeros
        std::vector<uint8_t> data(kLevelFileSize, 0);
        std::copy_n(levels_.begin() + std::ptrdiff_t((level - 1) * kLevelSize), kLevelSize, data.begin());
        return data;
    }
    auto it = files_.find(name);
    return it != files_.end() ? it->second : std::vector<uint8_t>();
}

bool GameFiles::extractTo(const std::string& dir) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (const Entry& e : kFiles) {
        std::vector<uint8_t> data = read(e.disk);
        if (data.empty() || !writeFile(fs::path(dir) / e.file, data)) {
            err_ = "cannot write " + (fs::path(dir) / e.file).string();
            return false;
        }
    }
    std::vector<uint8_t> levels;
    for (int level = 1; level <= kLevelCount; level++) {
        char name[8];
        std::snprintf(name, sizeof name, "PHIL_%02X", level + 0x0F);
        std::vector<uint8_t> data = read(name);
        if (data.size() < kLevelSize) { err_ = std::string("level file ") + name + " missing"; return false; }
        levels.insert(levels.end(), data.begin(), data.begin() + kLevelSize);
    }
    if (!writeFile(fs::path(dir) / kLevelsFile, levels)) {
        err_ = "cannot write " + (fs::path(dir) / kLevelsFile).string();
        return false;
    }
    return true;
}

}  // namespace amiga
