// Reader for Amiga OFS (old file system) floppy images (.adf, 880 KB).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace amiga {

class Adf {
public:
    bool open(const std::string& path);
    bool has(const std::string& name) const;
    // returns the file contents, empty if the file does not exist
    std::vector<uint8_t> read(const std::string& name) const;
    const std::string& error() const { return err_; }

private:
    const uint8_t* block(uint32_t n) const { return img_.data() + size_t(n) * 512; }
    static uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
    void scanDir(uint32_t blk, const std::string& prefix);

    std::vector<uint8_t> img_;
    std::map<std::string, uint32_t> files_;  // upper-case path -> header block
    std::string err_;
};

}  // namespace amiga
