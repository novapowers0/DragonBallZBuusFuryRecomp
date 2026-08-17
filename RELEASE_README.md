# Dragon Ball Z: Buu's Fury Recomp — v0.0.3 WIP (Release ejecutable)

Copyright (c) 2026 **NovaPowers**. Fork de
[`mstan/DragonBallZBuusFuryRecomp`](https://github.com/mstan/DragonBallZBuusFuryRecomp),
bajo la [PolyForm Noncommercial License 1.0.0](LICENSE).

Este paquete contiene el ejecutable recompilado de *Dragon Ball Z: Buu's Fury*
(GBA) con el launcher, el mod Adaptive Widescreen y el fix de crash del
overworld. **NO incluye la ROM ni la BIOS** (copyright): debes aportar los
archivos de tu copia legal.

## Contenido

- `DragonBallZBuusFuryRecomp.exe` — recompilador + launcher + mods
- `SDL2.dll`, `libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll` — dependencias runtime
- `overlay_toolchain/` — compilador para self-heal de cobertura en runtime
- `assets/`, `mods/` — recursos y mods del launcher (se generan al primer arranque)
- `RELEASE_README.md` — este archivo

## Cómo instalar y jugar

1. **Aporta los archivos del juego** (no se incluyen, son de tu copia legal):
   - Crea junto al exe los dumps que quieras usar. Cualquiera de estos tres es
     válido (mismo cart `BG3E`):
     - **USA** (RA / GoodTools): `Dragon Ball Z - Buu's Fury (U) [!].gba`
     - **USA** (No-Intro): `Dragon Ball Z - Buu's Fury (USA).gba`
     - **Español** (traducción): `Dragon Ball Z - Buu's Fury (Spanish).gba`
   - Aporta también tu **BIOS GBA retail** (`gba_bios.bin`).
   - Verifica los checksums en [`baserom.md`](baserom.md).
2. **Ejecuta** `DragonBallZBuusFuryRecomp.exe`.
3. En el launcher elige la ROM, la BIOS, y (si hay dump español) el idioma
   **EN/ES**; pulsa **Play**.
4. En **Mods** puedes activar el **Adaptive Widescreen** (v1.0.1).

## Novedades de este release (v0.0.3 WIP)

- **Fix del crash del overworld**: la zona inicial valida **FULLY STATIC**
  (sin bridge runaway). Se amplió la cobertura estática con +221 `extra_func`.
- **3 dumps equivalentes** (USA RA/GoodTools, USA No-Intro, español).
- **Launcher EN/ES** con etiqueta "USA + Mod SPA" y carátula española embebida.
- **crash.log** junto al exe ante cualquier fallo del runtime (para reportar).

## Bugs conocidos

- Preview experimental: no se ha probado de forma exhaustiva todo el juego.
  Haz copia de tus partidas.
- Las cajas de texto de algunas cutscenes pueden presentar artefactos al
  ensanchar la vista (investigación en curso).

## Legal

Proyecto no oficial, sin ánimo de lucro, de investigación y preservación. No
afiliado ni avalado por Nintendo, Atari, Webfoot Technologies, Shueisha, Toei
Animation ni ningún titular de los derechos de Dragon Ball. No se distribuye
ninguna ROM, BIOS, partida ni dato extraído del juego. Los archivos del juego
son de tu copia legal.

Repositorio: https://github.com/novapowers0/DragonBallZBuusFuryRecomp
