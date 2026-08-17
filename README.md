# Dragon Ball Z: Buu's Fury Recomp — v0.0.3 WIP

> **Fork** of [`mstan/DragonBallZBuusFuryRecomp`](https://github.com/mstan/DragonBallZBuusFuryRecomp)
> maintained by **NovaPowers** (see [Credits](#cr%C3%A9ditos) and
> [Fork of mstan](#fork-de-mstan)).
>
> Experimental preview of a static recompilation of **Dragon Ball Z: Buu's
> Fury** for Windows, with an optional **Adaptive Widescreen** mod. This fork
> adds crash fixes for the overworld, expanded static coverage of the starting
> zone, support for three equivalent ROM dumps (including the Spanish text
> translation), and an EN/ES launcher with the Spanish cover art.

The game ROM and Nintendo GBA BIOS are **not included**. You must provide your
own legally obtained dumps. This build accepts three equivalent dumps of the
same retail cart (`BG3E`, rev 0) — see [`baserom.md`](baserom.md) for the
exact hashes.

## Fork de mstan

Este repositorio es un **fork** de
[`mstan/DragonBallZBuusFuryRecomp`](https://github.com/mstan/DragonBallZBuusFuryRecomp),
el proyecto original de recompilación estática. El motor
[`gbarecomp`](https://github.com/mstan/gbarecomp) y el launcher
[`recomp-ui`](https://github.com/mstan/recomp-ui) también son forks propios
(`novapowers0/gbarecomp`, `novapowers0/recomp-ui`) que incorporan los cambios
de este proyecto. Todo el crédito del recompilador y la estructura original es
de **Matthew Stan (mstan)** y la comunidad **R.A.I.D. — Retro AI Development**;
este fork aporta mejoras específicas para Buu's Fury.

## Qué aporta este fork (v0.0.3)

| Cambio | Detalle |
|---|---|
| **Fix de crash en el overworld** | Un *bridge runaway* en la zona inicial (dispatch miss sin cobertura que el intérprete recorría 200M instrucciones y abortaba) se resolvió ampliando la cobertura estática. |
| **Cobertura estática expandida** | +221 `extra_func` de la zona inicial (targets de saltos computados que el finder AOT no descubría) fusionadas en `game.toml` y regeneradas → la zona inicial queda **FULLY STATIC** (sin bridge/overlay en runtime). |
| **Soporte de 3 dumps equivalentes** | RA/GoodTools (`f1c4...`), No-Intro (`e657...`) y traducción al español (`ee667...`); todos del mismo cart `BG3E`, un único código generado los sirve. |
| **Launcher EN/ES** | Selector de idioma EN/ES cuando se detecta el dump español, con etiqueta de región "USA + Mod SPA" y carátula española (ver Legal). |
| **Logger de crash** | El runtime escribe `crash.log` junto al exe con el PC del runaway y todos los dispatch misses de la sesión (útil para reportar). |
| **Mod v1.0.1** | Adaptive Widescreen actualizado por NovaPowers (view shift correcto, márgenes con tiles autoría, HUD anclado a los bordes). |

## Estado

La versión USA arranca su secuencia de atracción y entra en gameplay. Tras la
expansión de cobertura, la zona inicial del overworld valida **FULLY STATIC**
(`dispatch_misses=0`, `interpreted_insns=0`, `failed=0` en headless para USA y
la traducción española). Sigue siendo un preview experimental; no se ha
probado de forma exhaustiva todo el juego, así que haz copia de tus partidas.

## Quick start (jugadores)

1. Descarga el ZIP de Windows desde la pestaña **Releases** y extráelo.
2. Ejecuta `DragonBallZBuusFuryRecomp.exe`.
3. En el launcher selecciona tu ROM **Dragon Ball Z: Buu's Fury (USA)**, la
   alternativa No-Intro o la traducción española, y la BIOS GBA retail.
4. Configura pantalla, audio, controles y mods, y pulsa **Play**.

Si está presente la traducción española, el launcher muestra un selector
**EN/ES** junto a *Mods*: EN arranca el dump USA y ES el español, y además
cambia el idioma de la interfaz del launcher. La etiqueta de región es
"USA + Mod SPA" y la carátula cambia a la española.

El launcher recuerda los archivos válidos tras la primera configuración.
Marca **Skip launcher on boot** si quieres que los siguientes arranques vayan
directos al juego.

## Mod incluido

**Buu's Fury Adaptive Widescreen** (v1.0.1, actualizado por NovaPowers) es
opcional y está desactivado por defecto. Expande el HUD y el overworld al
redimensionar la ventana (hasta una vista lógica 480×160), haciendo streaming
de los chunks de campo autoría y ampliando la visibilidad de actores. No
estira la imagen original ni cambia coordenadas de cámara o mundo. La imagen
fiel 240×160 permanece intacta. Ábrelo en la página **Mods** del launcher.

## Características

- Aplicación nativa Windows x64
- Configuración de ROM y BIOS a través del launcher
  [recomp-ui](https://github.com/mstan/recomp-ui)
- Mod Adaptive Widescreen opcional con HUD anclado a los bordes
- Soporte de teclado y mando moderno
- Juego en ventana y pantalla completa con escalado nítido y filtrado opcional
- Menú de ajustes en el juego
- Guardados de cartucho y save states

## Controles

| Control GBA | Teclado |
|---|---|
| D-Pad | Flechas |
| A / B | Z / X |
| Start | Enter |
| Select | Backspace |

Usa **Shift+F1-F9** para guardar un estado y **F1-F9** para cargarlo. Los
controles se cambian desde el launcher.

## Building from source (desarrolladores)

Requisitos en Windows: CMake, Ninja, MSYS2 MinGW64 y SDL2. Clona con los
submódulos (apuntan a los forks del motor de este proyecto):

```powershell
git clone --recurse-submodules `
  https://github.com/novapowers0/DragonBallZBuusFuryRecomp.git
cd DragonBallZBuusFuryRecomp

cmake -S gbarecomp -B gbarecomp/build -G Ninja
cmake --build gbarecomp/build --target gba_recompile
pwsh tools/regen.ps1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target DragonBallZBuusFuryRecomp
```

La generación requiere la revisión de ROM soportada y una BIOS GBA retail. Sus
identidades y rutas de desarrollo local están documentadas en
[`baserom.md`](baserom.md) y [`game.toml`](game.toml). El código derivado de la
ROM, los inputs con copyright, las partidas y el output de build quedan
**locales y nunca se suben**.

> **Carátula española**: el exe de release lleva la carátula española embebida.
> En un checkout de fuente, `gbarecomp/src/runtime/cover_embedded.h` **no se
> distribuye** (contiene arte con copyright). El launcher compila igual sin
> ella (usa la carátula por defecto para el dump español); genérala localmente
> desde tu propia `cover.jpg` si quieres la carátula embebida.

## Legal

Proyecto no oficial, sin ánimo de lucro, de investigación y preservación. No
está afiliado ni avalado por Nintendo, Atari, Webfoot Technologies, Shueisha,
Toei Animation ni ningún titular de los derechos de Dragon Ball. Dragon Ball y
nombres, personajes, arte y datos del juego relacionados son marcas o
copyright de sus respectivos dueños.

**No se distribuye** ninguna ROM, BIOS GBA de Nintendo, partida ni dato
extraído del juego. La carátula de la caja del launcher se incluye con fines
de identificación. Ver [`baserom.md`](baserom.md) para los checksums y cómo
aportar tus propios archivos legales.

## Licencia

El código original de este proyecto se distribuye bajo la
[PolyForm Noncommercial License 1.0.0](LICENSE) (autoría de mstan para el
proyecto original; este fork la conserva). Las aportaciones de NovaPowers
también se publican bajo esa misma licencia.

## Créditos

- **Matthew Stan (mstan)** — recompilador gbarecomp, launcher recomp-ui y
  estructura del proyecto original.
- **R.A.I.D. — Retro AI Development** — comunidad de recompilación estática.
- **NovaPowers** — este fork: fix de crash, cobertura expandida, soporte de 3
  dumps, launcher EN/ES y actualización del mod Adaptive Widescreen a v1.0.1.

---

Parte de la comunidad **R.A.I.D. — Retro AI Development**.
