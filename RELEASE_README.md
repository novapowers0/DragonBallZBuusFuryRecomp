# Dragon Ball Z: Buu's Fury Recomp — v0.0.8 (Release ejecutable)

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
- `README.md` — guía rápida de instalación (en inglés, dentro del ZIP)
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
4. En **Mods** puedes activar el **Adaptive Widescreen** (v1.0.4).
5. Si el lanzador tarda en abrir (en algunos equipos Windows tarda en
   detectar los mandos), espera: no hace falta volver a abrirlo.

## Novedades de este release (v0.0.8)

**Correcciones**
- **Fusionarse ya no cierra el juego**: faltaban en el build estático unas 220
  rutinas de habilidades de Gotenks/Gogeta (y del menú de objetos); ahora están
  recompiladas. Todo el juego corre 100 % estático (sin compilar nada al vuelo)
  en las partidas probadas.
- **Fusión fallida + A en un círculo del mapa del mundo** ya no cuelga el juego,
  y se bloquea el exploit de las **255 Semillas del Ermitaño**. Ambas
  correcciones vienen activadas (`GBARECOMP_BF_FUSION_GUARD=0` /
  `GBARECOMP_BF_ITEM_GUARD=0` vuelven al comportamiento original).
- **Pantalla ancha**: desaparece la rejilla de puntos en los márgenes; el HUD se
  queda en el borde también mientras se ve el rótulo de la zona.
- **Perfil clásico 240 px**: el turbo con L, el avance con Select y el
  reproductor de música L+R funcionan también ahí, y mantener Select al caminar
  ya no pulsa A por su cuenta.
- Un estado guardado con el atajo de West City ya no se cuelga al cargarlo sin él.

**Pantalla ancha**
- Los logos ocupan todo el ancho y el **mapa del mundo** en vuelo extiende su
  cielo y la bruma del horizonte hasta los bordes.
- **Arte opcional de portada** para los márgenes (viñetas de manga nuevas en la
  portada española; cielo y Bola de Dragón en la USA), compuesto para cada ancho
  de ventana. Apagado por defecto; lo activa el perfil "extras" del lanzador.

**Contenido (menú de trucos, todo apagado por defecto)**
- **Todos los personajes de la historia en los círculos de guardado**: Goku,
  Gohan, Goten, Trunks y Vegeta desde cualquier círculo.
- **Gohan: forma Gran Saiyaman**, la forma sin terminar que quedó en el juego,
  y un truco que le devuelve la Super Saiyajin.

**Motor**
- Actualizado a gbarecomp y recomp-ui actuales: **audio estéreo** y alrededor de
  un **40 % más rápido** en el equipo de pruebas. Los estados guardados de la
  v0.0.7 siguen cargando (verificado fotograma a fotograma contra el build
  anterior).

Se mantiene de la versión anterior: arranque fiel al BIOS real, multiplicadores
de trucos, texto rápido, turbo con L, reproductor de música, menús y HUD a los
bordes, autoguardado con recuperación, estados F1–F9, avance rápido y rebobinado.

## Bugs conocidos

- Preview experimental: no se ha probado de forma exhaustiva todo el juego.
  Haz copia de tus partidas.
- Las cajas de texto de algunas cutscenes pueden presentar artefactos al
  ensanchar la vista (investigación en curso).
- Los personajes compuestos (Goku montado, vehículos, etc.) pueden verse
  ligeramente desplazados; también ocurre a 240 px.

## Legal

Proyecto no oficial, sin ánimo de lucro, de investigación y preservación. No
afiliado ni avalado por Nintendo, Atari, Webfoot Technologies, Shueisha, Toei
Animation ni ningún titular de los derechos de Dragon Ball. No se distribuye
ninguna ROM, BIOS, partida ni dato extraído del juego. Los archivos del juego
son de tu copia legal.

Repositorio: https://github.com/novapowers0/DragonBallZBuusFuryRecomp
