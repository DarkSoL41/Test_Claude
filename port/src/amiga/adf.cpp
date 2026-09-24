#include "adf.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace amiga {

namespace {
std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

bool Adf::open(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err_ = "cannot open " + path; return false; }
    img_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (img_.size() < 901120) { err_ = path + ": not an 880 KB ADF image"; return false; }
    files_.clear();
    scanDir(880, "");
    if (files_.empty()) { err_ = path + ": no files found (not an OFS disk?)"; return false; }
    return true;
}

void Adf::scanDir(uint32_t blk, const std::string& prefix) {
    const uint8_t* b = block(blk);
    for (int i = 0; i < 72; i++) {
        uint32_t h = be32(b + 24 + 4 * i);
        int guard = 0;
        while (h && h < 1760 && guard++ < 1000) {
            const uint8_t* hb = block(h);
            int nl = hb[432];
            if (nl > 30) nl = 30;
            std::string name(reinterpret_cast<const char*>(hb + 433), nl);
            int32_t type = int32_t(be32(hb + 508));
            if (type == 2) scanDir(h, prefix + name + "/");
            else files_[upper(prefix + name)] = h;
            h = be32(hb + 496);
        }
    }
}

bool Adf::has(const std::string& name) const { return files_.count(upper(name)) != 0; }

std::vector<uint8_t> Adf::read(const std::string& name) const {
    auto it = files_.find(upper(name));
    if (it == files_.end()) return {};
    const uint8_t* hb = block(it->second);
    uint32_t size = be32(hb + 0x144);
    std::vector<uint8_t> out;
    out.reserve(size);
    uint32_t data = be32(hb + 16);  // first data block
    int guard = 0;
    while (data && data < 1760 && out.size() < size && guard++ < 2000) {
        const uint8_t* db = block(data);
        uint32_t n = std::min<uint32_t>(be32(db + 12), 488);
        out.insert(out.end(), db + 24, db + 24 + n);
        data = be32(db + 16);
    }
    if (out.size() > size) out.resize(size);
    return out;
}

}  // namespace amiga
