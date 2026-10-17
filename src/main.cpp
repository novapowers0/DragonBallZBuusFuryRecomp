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
     "8300155C E0FF\n8300155E 05F5",
     "Cheats - Aids"},
    {"Infinite HP (all)",
     "Keeps every character's HP and max HP at 9999.",
     "43001576 270F\n00000008 0068\n43001578 270F\n00000008 0068",
     "Cheats - Aids"},
    {"Infinite EP (all)",
     "Keeps every character's EP and max EP at 9999.",
     "4300157A 270F\n00000008 0068\n4300157C 270F\n00000008 0068",
     "Cheats - Aids"},
    {"Max EXP (all)",
     "Sets every character's EXP to its maximum.",
     "43001584 C9FF\n00000008 0068\n43001586 3B9A\n00000008 0068",
     "Cheats - Aids"},
    {"Infinite fusion time",
     "Holds the fusion timer so a fusion never runs out.",
     "83001840 466D",
     "Cheats - Aids"},
    {"All Dragon Balls found",
     "Marks every Dragon Ball as collected. Turn it off and save after the "
     "Dragon Ball quest so the story keeps progressing.",
     "830010CA FFFF",
     "Cheats - Aids"},
    {"Walk Anywhere (test tool)",
     "Lets the character walk through walls so you can reach any area for "
     "testing. Community CodeBreaker code for this cart.",
     "83001200 5908\n83001202 B43F\n83001204 4811\n83001206 6800\n"
     "83001208 2800\n8300120A D01B\n8300120C 21FF\n8300120E 23A1\n"
     "83001210 18C9\n83001212 1840\n83001214 2320\n83001216 6804\n"
     "83001218 6845\n8300121A 46C0\n8300121C 490C\n8300121E 7809\n"
     "83001220 297F\n83001222 D006\n83001224 29BF\n83001226 D006\n"
     "83001228 29DF\n8300122A D006\n8300122C 29EF\n8300122E D006\n"
     "83001230 E008\n83001232 18ED\n83001234 E004\n83001236 1AED\n"
     "83001238 E002\n8300123A 1AE4\n8300123C E000\n8300123E 18E4\n"
     "83001240 6004\n83001242 6045\n83001244 BC3F\n83001246 46C0\n"
     "83001248 4C02\n8300124A 4700\n8300124C 1C34\n8300124E 0300\n"
     "83001250 0130\n83001252 0400\n83001254 0C7C\n83001256 0300\n"
     "83000C70 2000\n83000C72 E59F\n83000C74 FF12\n83000C76 E12F\n"
     "83000C78 1201\n83000C7A 0300",
     "Cheats - Aids"},
    // Dialogue text printing is paced by a per-character countdown in the
    // text engine (0x030038A4, observed counting 4..1 while printing): a
    // character prints when the countdown reaches zero. Re-asserting 1 every
    // frame turns that into "print every frame", measured at roughly three
    // times the original pace in the intro scene (42 vs 13 characters per 60
    // frames). Re-asserting 2 does NOT give "x2": the engine compares against
    // zero after its own decrement, so the value never lands on zero and text
    // crawls instead. One preset, value 1.
    {"Fast text (x3)",
     "Dialogue prints about three times as fast as the original pace.",
     "330038A4 0001",
     "Cheats - Dialogue"},
};

// Gain multipliers: instead of pinning a value, these scale every gain the
// guest makes (the engine watches the live value and writes back
// last + gain x factor). They seed themselves when enabled, so turning one on
// mid-game never multiplies what you already have, and losses/spending pass
// through untouched. Addresses are the shared BG3E layout: EXP is a 32-bit
// value per character (stride 0x68, the same eight slots the "Max EXP (all)"
// code writes); Zenie is one 32-bit wallet.
const gbarecomp::CheatMultiplierTarget kExpGainX2[] = {
    {0x03001584u, 2, 999999999u}, {0x030015ECu, 2, 999999999u},
    {0x03001654u, 2, 999999999u}, {0x030016BCu, 2, 999999999u},
    {0x03001724u, 2, 999999999u}, {0x0300178Cu, 2, 999999999u},
    {0x030017F4u, 2, 999999999u}, {0x0300185Cu, 2, 999999999u},
};
const gbarecomp::CheatMultiplierTarget kExpGainX4[] = {
    {0x03001584u, 4, 999999999u}, {0x030015ECu, 4, 999999999u},
    {0x03001654u, 4, 999999999u}, {0x030016BCu, 4, 999999999u},
    {0x03001724u, 4, 999999999u}, {0x0300178Cu, 4, 999999999u},
    {0x030017F4u, 4, 999999999u}, {0x0300185Cu, 4, 999999999u},
};
const gbarecomp::CheatMultiplierTarget kZenieGainX2[] = {
    {0x0300155Cu, 2, 99999999u},
};
const gbarecomp::CheatMultiplierTarget kZenieGainX4[] = {
    {0x0300155Cu, 4, 99999999u},
};

// Optional difficulty and progression presets (all off by default, like every
// other cheat). The "damage taken" targets watch each character's current HP
// (16-bit, stride 0x68, the same eight slots as EXP) and scale only the
// *losses*: heals are gains and pass through untouched. "Slow XP curve" halves
// experience gains for players who want a harder progression, in the spirit
// of the Hard Way rebalance.
const gbarecomp::CheatMultiplierTarget kDamageTakenX2[] = {
    {0x03001576u, 1, 0, 1, 2, 1, 2}, {0x030015DEu, 1, 0, 1, 2, 1, 2},
    {0x03001646u, 1, 0, 1, 2, 1, 2}, {0x030016AEu, 1, 0, 1, 2, 1, 2},
    {0x03001716u, 1, 0, 1, 2, 1, 2}, {0x0300177Eu, 1, 0, 1, 2, 1, 2},
    {0x030017E6u, 1, 0, 1, 2, 1, 2}, {0x0300184Eu, 1, 0, 1, 2, 1, 2},
};
const gbarecomp::CheatMultiplierTarget kDamageTakenHalf[] = {
    {0x03001576u, 1, 0, 1, 1, 2, 2}, {0x030015DEu, 1, 0, 1, 1, 2, 2},
    {0x03001646u, 1, 0, 1, 1, 2, 2}, {0x030016AEu, 1, 0, 1, 1, 2, 2},
    {0x03001716u, 1, 0, 1, 1, 2, 2}, {0x0300177Eu, 1, 0, 1, 1, 2, 2},
    {0x030017E6u, 1, 0, 1, 1, 2, 2}, {0x0300184Eu, 1, 0, 1, 1, 2, 2},
};
const gbarecomp::CheatMultiplierTarget kExpSlow[] = {
    {0x03001584u, 1, 999999999u, 2, 1, 1, 4},
    {0x030015ECu, 1, 999999999u, 2, 1, 1, 4},
    {0x03001654u, 1, 999999999u, 2, 1, 1, 4},
    {0x030016BCu, 1, 999999999u, 2, 1, 1, 4},
    {0x03001724u, 1, 999999999u, 2, 1, 1, 4},
    {0x0300178Cu, 1, 999999999u, 2, 1, 1, 4},
    {0x030017F4u, 1, 999999999u, 2, 1, 1, 4},
    {0x0300185Cu, 1, 999999999u, 2, 1, 1, 4},
};

const gbarecomp::CheatMultiplierSpec kCheatMultipliers[] = {
    {"EXP x2 (all)",
     "Every experience gain counts double for all characters.",
     kExpGainX2, 8, "Cheats - Multipliers"},
    {"EXP x4 (all)",
     "Every experience gain counts four times for all characters.",
     kExpGainX4, 8, "Cheats - Multipliers"},
    {"Zenie x2",
     "Every Zenie you earn counts double. Spending and losses are unchanged.",
     kZenieGainX2, 1, "Cheats - Multipliers"},
    {"Zenie x4",
     "Every Zenie you earn counts four times. Spending and losses are "
     "unchanged.",
     kZenieGainX4, 1, "Cheats - Multipliers"},
    {"Damage taken x2 (hard)",
     "Hard mode: every hit costs twice as much HP. Heals are unchanged.",
     kDamageTakenX2, 8, "Cheats - Difficulty"},
    {"Damage taken x0.5 (easy)",
     "Easy mode: every hit costs half as much HP. Heals are unchanged.",
     kDamageTakenHalf, 8, "Cheats - Difficulty"},
    {"Slow XP curve (Hard Way)",
     "Every experience gain counts half, closer to the Hard Way rebalance.",
     kExpSlow, 8, "Cheats - Difficulty"},
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
    {"Attack (slot 1)", "Attack/strength of the first character.",
     0x03001588u, 16, 0, 65535, 100},
    {"Dragon Balls", "Dragon Ball flags (all found = 65535).",
     0x030010CAu, 16, 0, 65535, 1},
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

    // Startup logos (legal, ATARI, FUNimation/Webfoot) run in Assist Tools
    // fast-forward until the title screen is up: the guest frame sequence is
    // untouched, only presentation pacing changes, so by guest frame 500 the
    // player is looking at "PRESS START" after about a second instead of
    // eight. GBARECOMP_TURBO_BOOT overrides the frame count (0 disables).
    opts.auto_fast_forward_until_frame = 500;
    if (const char* turbo_boot = std::getenv("GBARECOMP_TURBO_BOOT")) {
        const int frames = std::atoi(turbo_boot);
        opts.auto_fast_forward_until_frame =
            frames > 0 ? static_cast<std::uint64_t>(frames) : 0;
    }

    // Player-facing cheats: an in-game "Cheats" menu of optional CodeBreaker
    // codes (all disabled by default).
    opts.expose_cheats = true;
    opts.cheat_presets = kCheats;
    opts.cheat_preset_count = sizeof(kCheats) / sizeof(kCheats[0]);
    opts.cheat_multipliers = kCheatMultipliers;
    opts.cheat_multiplier_count =
        sizeof(kCheatMultipliers) / sizeof(kCheatMultipliers[0]);

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
