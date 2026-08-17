# Required binaries

Provide these files locally; neither is distributed by this project.

## Dragon Ball Z: Buu's Fury (USA)

Two equivalent USA dumps of the same retail cart (game code `BG3E`,
revision 0) are supported. They differ only in the 0x00-0xBF GBA header
region (title / maker / complement bytes), so one generated image serves
both. Either may be dropped at `roms/dragon_ball_z_buus_fury_usa.gba`.

### Primary — GoodTools / RetroAchievements dump (used to regenerate)

- Common names: `Dragon Ball Z - Buu's Fury (U) [!].gba`,
  `dragon ball z - buu's fury (usa).bin`
- Path: `roms/dragon_ball_z_buus_fury_usa.gba`
- Game code: `BG3E`
- Revision: 0
- Size: 8,388,608 bytes
- CRC32: `01C1707F`
- MD5: `3a74fce97f1ea2b28c2a50ec3df0acee`
- SHA-1: `f1c4b07554d2a3b1ad2f325307051e775ce68087`

### Alternative — No-Intro dump

- Common name: `Dragon Ball Z - Buu's Fury (USA).gba`
- Path: `roms/dragon_ball_z_buus_fury_usa.gba`
- Game code: `BG3E`
- Revision: 0
- Size: 8,388,608 bytes
- CRC32: `DCF23468`
- MD5: `808eda67b17c901e9052f47bdc419e2f`
- SHA-1: `e65738e9d67688309f09811a54f495523ec9aada`
- SHA-256: `15df2fbbe072cc5d28a699ae9af9c56657e290cdb47c92e57d6ce14a01be4b64`

### Alternative — Spanish text translation

- Common name: `Dragon Ball Z - Buu's Fury (Spanish).gba`
- Code-identical to the retail USA cart; only the GBA header and text
  strings differ, so the same generated image runs it.
- Game code: `BG3E`
- Revision: 0
- Size: 10,514,228 bytes
- MD5: `dbcc837952a7d7f12da0684af400db9c`
- SHA-1: `ee667f656f6c3a128b9d59c657b3686e58b23eb5`

## Game Boy Advance BIOS

- Path: `gbarecomp/bios/gba_bios.bin`
- SHA-1: `300c20df6731a33952ded8c436f7f186d25d3492`
