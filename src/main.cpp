#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "buus_fury_extended_view.h"
#include "runtime.h"

#if defined(GBAGAME_RECOMP_UI)
#include "game_launcher_boot.h"
#endif

namespace {

void print_usage() {
    std::printf(
        "DragonBallZBuusFuryRecomp [--bios <path>] [--rom <path>] "
        "[game.toml]\n");
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    gbarecomp::RunOptions opts;
    opts.builtin_game_name = "Dragon Ball Z: Buu's Fury";
    // Accepted USA dumps of the same retail cart (BG3E, rev 0). The primary is
    // the GoodTools / RetroAchievements dump ("Dragon Ball Z - Buu's Fury (U)
    // [!].gba", CRC32 01C1707F), which the generated code is built from; the
    // alternatives are the No-Intro dump (CRC32 DCF23468) and the Spanish
    // text-translation ("Dragon Ball Z - Buu's Fury (Spanish).gba"), which
    // differ only in header / text-data bytes, so one generated image serves
    // all three dumps.
    opts.builtin_rom_sha1 = "f1c4b07554d2a3b1ad2f325307051e775ce68087";
    opts.builtin_rom_crc32 = 0x01C1707Fu;
    static const char* const kAltRomSha1[] = {
        "e65738e9d67688309f09811a54f495523ec9aada",  // No-Intro dump
        "ee667f656f6c3a128b9d59c657b3686e58b23eb5",  // Spanish translation
    };
    opts.builtin_rom_sha1_alts = kAltRomSha1;
    opts.num_builtin_rom_sha1_alts = 2;
    opts.mod_game_id = "dragon-ball-z-buus-fury-us";
    opts.mod_owns_adaptive_view = true;
    opts.max_view_width = 480;
    opts.max_resize_view_width = 480;
    opts.resize_driven_view = true;
    opts.extended_view_init = &buus_fury::install_extended_view;
    opts.launcher_expose_widescreen = false;
    opts.launcher_expose_adaptive_view = false;
    opts.launcher_region = "USA";
    opts.launcher_game_config = "game.toml";

#if defined(GBAGAME_RECOMP_UI)
    std::vector<std::string> args(argv, argv + argc);
    if (game_launcher_preboot(args, opts)) return 0;
    std::vector<char*> av;
    av.reserve(args.size());
    for (auto& arg : args) av.push_back(arg.data());
    return gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
#else
    return gbarecomp::run_game(argc, argv, opts);
#endif
}
