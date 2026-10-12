#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "buus_fury_extended_view.h"
#include "runtime.h"

#if defined(GBAGAME_RECOMP_UI)
#include "game_launcher_boot.h"
#endif

namespace {

// Player-facing cheats: curated CodeBreaker Advance codes for this cart
// (BG3E). Every one starts off; enabling one in the in-game "Cheats" menu
// re-applies it once per frame. RAM addresses are the shared BG3E layout, so
// they serve the USA dump and the Spanish translation alike.
const gbarecomp::CheatPreset kCheats[] = {
    {"Max Zenie",
     "Sets your money to 99,999,999.",
     "8300155C E0FF\n8300155E 05F5"},
    {"Infinite HP (all)",
     "Keeps every character's HP and max HP at 9999.",
     "43001576 270F\n00000008 0068\n43001578 270F\n00000008 0068"},
    {"Infinite EP (all)",
     "Keeps every character's EP and max EP at 9999.",
     "4300157A 270F\n00000008 0068\n4300157C 270F\n00000008 0068"},
    {"Max EXP (all)",
     "Sets every character's EXP to its maximum.",
     "43001584 C9FF\n00000008 0068\n43001586 3B9A\n00000008 0068"},
    {"Infinite fusion time",
     "Holds the fusion timer so a fusion never runs out.",
     "83001840 466D"},
    {"All Dragon Balls found",
     "Marks every Dragon Ball as collected. Turn it off and save after the "
     "Dragon Ball quest so the story keeps progressing.",
     "830010CA FFFF"},
};

// In-game Editor: numeric fields the player can read and change directly.
// Same shared BG3E RAM layout as the cheats (first character slot = the stat
// block the cheat codes write first).
const gbarecomp::GbaEditField kEditFields[] = {
    {"Zenie", "Your money.", 0x0300155Cu, 32, 0, 99999999, 1000},
    {"HP (slot 1)", "Current HP of the first character.",
     0x03001576u, 16, 0, 9999, 100},
    {"Max HP (slot 1)", "Maximum HP of the first character.",
     0x03001578u, 16, 0, 9999, 100},
    {"EP (slot 1)", "Current EP of the first character.",
     0x0300157Au, 16, 0, 9999, 100},
    {"Max EP (slot 1)", "Maximum EP of the first character.",
     0x0300157Cu, 16, 0, 9999, 100},
    {"EXP (slot 1)", "Experience of the first character.",
     0x03001584u, 32, 0, 999999999, 1000},
};

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

    // Player-facing quality of life: an in-game Assist Tools menu with save
    // states, fast-forward and a short rewind buffer, plus a periodic autosave
    // crash guard that resumes the last snapshot after an unclean exit.
    opts.expose_assist_tools = true;
    opts.assist_tools_enabled_by_default = true;
    opts.rewind_history_seconds = 8;
    opts.rewind_capture_interval_frames = 15;
    opts.autosave_interval_seconds = 30;
    opts.resume_suspend_state_on_launch = true;
    // Press Start + Select in game to open the runtime menu (cheats, save
    // states, fast-forward, rewind).
    opts.in_game_menu_start_select = true;

    // Player-facing cheats: an in-game "Cheats" menu of optional CodeBreaker
    // codes (all disabled by default).
    opts.expose_cheats = true;
    opts.cheat_presets = kCheats;
    opts.cheat_preset_count = sizeof(kCheats) / sizeof(kCheats[0]);

    // In-game Editor: read/change money and the first character's stats.
    opts.expose_editor = true;
    opts.edit_fields = kEditFields;
    opts.edit_field_count = sizeof(kEditFields) / sizeof(kEditFields[0]);

    std::vector<std::string> args(argv, argv + argc);

#if defined(GBAGAME_RECOMP_UI)
    if (game_launcher_preboot(args, opts)) return 0;
#endif

    // Fast start: skip the GBA boot logo/chime by default. This is a
    // presentation choice only — the recompiled BIOS stays linked and still
    // services IRQs and SWIs, so no guest semantics change. An explicit
    // --bios-skip-intro/--no-bios-skip-intro flag or GBARECOMP_BIOS_SKIP_INTRO
    // in the environment restores the authentic intro.
    bool intro_choice_given = false;
    for (const std::string& a : args) {
        if (a == "--bios-skip-intro" || a == "--no-bios-skip-intro" ||
            a == "--bios-hle" || a == "--no-bios-hle" ||
            a == "--bios-hle-keep-intro") {
            intro_choice_given = true;
            break;
        }
    }
    if (!intro_choice_given &&
        std::getenv("GBARECOMP_BIOS_SKIP_INTRO") == nullptr) {
        args.emplace_back("--bios-skip-intro");
    }

    std::vector<char*> av;
    av.reserve(args.size());
    for (std::string& arg : args) av.push_back(arg.data());
    return gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
}
