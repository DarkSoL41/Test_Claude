// Hiscore file (PHIL_03, 2048 bytes): a "clean" variant without the players
// and CRYSTAL records the cracked disk image shipped with.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace game {

// Built in the format the game itself produces when a player is deleted
// (menu_delete_player, $9DDA): the player's name in the list becomes
// "--------", the 22-byte record is cleared, and the "CURRENT POSITIONS"
// lines of empty records read "000 -------- 000:00:00".
inline std::vector<uint8_t> cleanHiscores() {
    std::vector<uint8_t> f(2048, 0);
    // $000: 20 position lines of 22 characters
    for (int i = 0; i < 20; i++) std::memcpy(&f[i * 22], "000 -------- 000:00:00", 22);
    // $1B8: player list, 20 x (word index + 8-character name)
    for (int i = 0; i < 20; i++) {
        uint8_t* e = &f[0x1B8 + i * 10];
        e[0] = 0;
        e[1] = uint8_t(i);
        std::memcpy(e + 2, "--------", 8);
    }
    // $280: 20 player records of 22 bytes: all free (zero)
    // $438: hall of fame, 3 lines of 18 characters
    for (int i = 0; i < 3; i++) std::memcpy(&f[0x438 + i * 18], "-------- 999:59:59", 18);
    return f;
}

}  // namespace game
