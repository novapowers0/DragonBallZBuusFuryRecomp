# Dragon Ball Z: Buu's Fury Recomp

![Plataforma](https://img.shields.io/badge/plataforma-Windows%20x64-blue)
![Estado](https://img.shields.io/badge/estado-preview%20experimental-orange)
![Licencia](https://img.shields.io/badge/licencia-PolyForm%20Noncommercial%201.0.0-lightgrey)
![Comunidad](https://img.shields.io/badge/R.A.I.D.-Discord-7289da)

Recompilación estática de **Dragon Ball Z: Buu's Fury** (Game Boy Advance) a una
aplicación nativa de Windows, con **pantalla ancha adaptativa** opcional y varias
mejoras de calidad de vida. La lógica original del juego se traduce a código
nativo en lugar de ejecutarse dentro de un emulador.

> **Fork** de [`mstan/DragonBallZBuusFuryRecomp`](https://github.com/mstan/DragonBallZBuusFuryRecomp)
> mantenido por **NovaPowers**. El motor [`gbarecomp`](https://github.com/mstan/gbarecomp)
> y el launcher [`recomp-ui`](https://github.com/mstan/recomp-ui) son también
> forks propios que incorporan los cambios de este proyecto. Todo el mérito del
> recompilador y de la estructura original es de **Matthew Stan (mstan)** y la
> comunidad **R.A.I.D. — Retro AI Development**.

**No se incluye** ninguna ROM ni la BIOS de Nintendo. Debes aportar los archivos
de tu copia legal. Se aceptan tres dumps equivalentes del mismo cartucho
(`BG3E`, rev 0), incluida la traducción al español — consulta
[`baserom.md`](baserom.md) para los hashes exactos.

---

## Lo más destacado

- **Pantalla ancha adaptativa** (mod opcional): revela mundo autoría real en los
  márgenes al ensanchar la ventana, sin estirar la imagen ni tocar la cámara.
- **Autosave con recuperación**: guarda una instantánea cada 30 s y reanuda la
  partida tras un cierre inesperado.
- **Herramientas de partida**: estados de guardado, avance rápido y rebobinado.
- **Arranque rápido**: salta la intro del BIOS (reversible).
- **Cinco perfiles de color de pantalla**: Raw, Unlit, Frontlit, Backlit y Classic.
- **Launcher EN/ES** con carátula española cuando se detecta la traducción.
- **Audio corregido** (el crujido constante del mezclador original).

## Novedades de este fork

| Cambio | Detalle |
|---|---|
| **Arranque rápido** | Salta la intro del BIOS por defecto (presentación; el BIOS real sigue ejecutándose para IRQ/SWI). Reversible con `--no-bios-skip-intro`. |
| **Autosave + recuperación** | Instantánea periódica junto a la ROM; si el proceso muere sin salir limpiamente, el siguiente arranque reanuda. Una salida normal no reanuda. |
| **Herramientas en el menú** | Estados (F1–F9 / Shift+F1–F9), avance rápido y rebobinado de 8 s, conmutables desde el menú del juego. |
| **Audio arreglado** | El motor mezcla a 32.768 Hz pero se reproducía como si fueran 65.536; el búfer quedaba siempre vacío y sonaba "petardeado". Ahora suena limpio. |
| **Fix de crash en el overworld** | Un *bridge runaway* en la zona inicial se resolvió ampliando la cobertura estática. |
| **Cobertura estática ampliada** | La zona inicial del overworld valida **FULLY STATIC** (0 misses, 0 interpretadas). |
| **Tres dumps equivalentes** | GoodTools/RA (`f1c4…`), No-Intro (`e657…`) y traducción al español (`ee667…`); un único código generado los sirve. |
| **Launcher EN/ES** | Selector de idioma cuando hay dump español, etiqueta de región "USA + Mod SPA" y carátula española. |

## Estado

Preview **experimental**. La versión USA arranca, pasa la secuencia de atracción
y entra en gameplay; la validación estricta cubre atracción, gameplay y
recorridos sin instrucciones interpretadas ni dispatch misses. No se ha probado
de forma exhaustiva todo el juego, así que **haz copia de tus partidas**.

## Quick start

1. Descarga el ZIP de Windows desde la pestaña **Releases** y extráelo.
2. Ejecuta `DragonBallZBuusFuryRecomp.exe`.
3. En el launcher elige tu ROM (USA, No-Intro o la traducción española) y la
   BIOS GBA retail.
4. Ajusta pantalla, audio, controles y mods, y pulsa **Play**.

Si está presente la traducción española, aparece un selector **EN/ES** junto a
*Mods*. El launcher recuerda los archivos válidos; marca **Skip launcher on
boot** para ir directo al juego en los siguientes arranques.

## Mod de pantalla ancha

**Buu's Fury Adaptive Widescreen** es opcional y está **desactivado por
defecto**. Al ensanchar la ventana, la vista lógica crece de 240×160 hasta
480×160 revelando mundo autoría real en los márgenes (no es un estirado ni un
recorte), con streaming de los chunks de campo y visibilidad de actores
ampliada. La imagen fiel **240×160 permanece idéntica** con el mod apagado.
Actívalo en la página **Mods** del launcher.

## Controles y atajos

| Control GBA | Teclado |
|---|---|
| D-Pad | Flechas |
| A / B | Z / X |
| Start | Enter |
| Select | Backspace |

- **Shift+F1–F9**: guardar estado · **F1–F9**: cargar estado.
- **Avance rápido** y **rebobinado**: desde el menú de herramientas del juego.
- **Autosave**: automático cada 30 s mientras juegas en ventana.

Los controles se cambian desde el launcher.

## Compilar desde el código

Requisitos en Windows: CMake, Ninja, MSYS2 MinGW64 y SDL2.

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

La generación requiere la revisión de ROM soportada y una BIOS GBA retail; sus
identidades y rutas locales están en [`baserom.md`](baserom.md) y
[`game.toml`](game.toml). El código derivado de la ROM, los inputs con
copyright, las partidas y el output de build quedan **locales y nunca se suben**.

> **Carátula española**: el exe de release la lleva embebida. En un checkout de
> fuente, `gbarecomp/src/runtime/cover_embedded.h` no se distribuye (arte con
> copyright); el launcher compila igual sin ella.

## Verificación

`tools/regression_check.ps1` activa el mod de pantalla ancha, ejecuta el juego
sin ventana a 480 px y a 240 px desde los mismos estados, y comprueba que la
franja central de la imagen ancha coincide con la nativa (la mejora debe ser un
superconjunto exacto). Exige además que el fotograma realmente se ensanche.
Resultado del ejecutable de release: **48/48 escenas al 0,0000 %**.

## Legal

Proyecto no oficial, sin ánimo de lucro, de investigación y preservación. No
está afiliado ni avalado por Nintendo, Atari, Webfoot Technologies, Shueisha,
Toei Animation ni ningún titular de los derechos de Dragon Ball. Dragon Ball y
los nombres, personajes, arte y datos del juego relacionados son marcas o
copyright de sus respectivos dueños.

**No se distribuye** ninguna ROM, BIOS GBA de Nintendo, partida ni dato extraído
del juego. La carátula del launcher se incluye con fines de identificación.
Consulta [`baserom.md`](baserom.md) para los checksums y cómo aportar tus
propios archivos legales.

## Licencia

El código original de este proyecto se distribuye bajo la
[PolyForm Noncommercial License 1.0.0](LICENSE) (autoría de mstan para el
proyecto original; este fork la conserva). Las aportaciones de NovaPowers se
publican bajo esa misma licencia.

## Créditos

- **Matthew Stan (mstan)** — recompilador `gbarecomp`, launcher `recomp-ui` y
  estructura del proyecto original.
- **R.A.I.D. — Retro AI Development** — comunidad de recompilación estática.
- **NovaPowers** — este fork: fix de crash, cobertura ampliada, soporte de tres
  dumps, launcher EN/ES, arreglo de audio, arranque rápido, autosave,
  herramientas de partida y actualización del mod Adaptive Widescreen.

---

Parte de la comunidad **R.A.I.D. — Retro AI Development**.
