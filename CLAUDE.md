# CLAUDE.md - DragonBallZBuusFuryRecomp

This game repository defers platform behavior and debugging rules to:

- `gbarecomp/CLAUDE.md`
- `gbarecomp/PRINCIPLES.md`
- `gbarecomp/DEBUG.md`
- `gbarecomp/TCP.md` (this game uses port 19889)

## Build target

Initialize both submodules before building. Generated cart code is intentionally
not committed and must be recreated with `tools/regen.ps1` from a verified
user-supplied USA ROM. Build with MSYS2 mingw64, CMake, and Ninja. Keep large
generated translation units at modest parallelism and run compilation at low
priority.

## Game-specific rules

1. Two equivalent USA dumps of the same retail cart (`BG3E`, rev 0) are
   supported: the GoodTools / RetroAchievements dump (SHA-1
   `f1c4b07554d2a3b1ad2f325307051e775ce68087`, CRC32 `01C1707F`) and the
   No-Intro dump (SHA-1
   `e65738e9d67688309f09811a54f495523ec9aada`, CRC32 `DCF23468`). They
   differ only in the 0x00-0xBF header region, so one generated image
   serves both. The Spanish text translation (SHA-1
   `ee667f656f6c3a128b9d59c657b3686e58b23eb5`) is the same cart with a
   different text region (12 pointer bytes + text data), so it also boots
   the identical image. Do not weaken the identity gate beyond this
   reviewed set.
2. ROMs, BIOS images, generated C/C++, saves, screenshots, and runtime caches
   are local artifacts and must never be committed.
3. Generated code is never edited. Fix `game.toml`, the recompiler, or the
   runtime and regenerate.
4. Adaptive widescreen is a game-owned optional mod. Native 240x160 rendering
   must remain unchanged when the feature is disabled.
5. Overworld extensions must resolve authored field-resource chunks. Do not
   repeat the hardware tilemap ring into expanded margins.
6. Visibility patches must target exact renderer reads. Do not mutate camera
   state or actor world coordinates to make expanded actors appear.
7. The launcher seam (gbarecomp/src/runtime/launcher_seam.h) scans the exe
   directory for the accepted dumps and, when the Spanish dump is present,
   exposes an EN/ES boot selector and the "USA + Mod SPA" region label for
   every dump (independent of the selected language). The Spanish box art is
   EMBEDDED in the executable (gbarecomp/src/runtime/cover_embedded.h,
   generated from cover.jpg) and materialized to assets/img/boxart_spanish.jpg
   on first launch — there is no loose cover.jpg to keep beside the exe. The
   cover path string must live in a std::string that outlives the launcher
   window (never `cover_out.string().c_str()` as a temporary — dangling).
   Toggling ES also localizes the launcher UI via tr_ui() in launcher_imgui.cpp
   (g_ui_lang mirrors s.language_index). The boot ROM for the committed
   language is authoritative; keep the identity gate intact. Regenerating
   cover_embedded.h requires re-running the byte-array generator over a fresh
   cover.jpg (FFD8 JPEG; see build note in the header). Mod version bumps
   require renaming the package version DIRECTORY (mod_runtime.cpp validates
   manifest.version == directory name — a manifest-only bump silently kills
   the mod and its header buttons).
8. The field-object cache scan in buus_fury_extended_view.cpp must stay gated
   to once per composed frame. A per-pixel EWRAM sweep when DISPCNT is
   field-shaped but layer objects are not yet created freezes the game at
   new-game/area transitions.
 9. Adaptive view uses the boundary-framing view shift (the published mod's
    approach, shared with Legacy II, same Webfoot engine): g_ws_bg_x_provider
    computes a per-frame g_render_view_shift that slides the NATIVE viewport
    within the wider host surface only when the camera approaches a field edge,
    so the margins always fall inside authored content (zero shift in ordinary
    areas). The tilemap provider fills margins with authored field tiles and
    returns kWsTilemapUnavailable outside the field — never a clamp/KeepWrapped
    fallback (those manufacture a repeated/mirrored edge or show the 256px ring
    alias, which look wrong, especially as a black right margin). The frame-start
    housekeeping (view-shift recalculation + actor-tracking generation) lives in
    bg_x_provider at the first visited pixel (bg==3, output_x==0, screen_y==0).
    Do NOT switch to g_ws_bg_x_provider = nullptr: the nullptr path was tried
    and produced broken margin fills, a black right edge near the field boundary,
    and stale tiles in-game.
