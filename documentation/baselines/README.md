# Baselines del harness PPU

`ppu-v2-baseline-2026-11-12.tsv` es el baseline de frames de la PPU V2
(código de `master`, rama `ppu-v3` con el harness de F0). Es la referencia para
F1: el scheduler de 4 fases **no debe cambiar ni un solo diff**.

Generado con:

```sh
QUIET=1 REPORT=/tmp/qtrebg_frames/baseline-full-2026-11-12.tsv \
    ./mts/compare_ppu_frames.sh
```

Columnas: `name`, `status`, `diff`. El `diff` es el número de píxeles distintos
(ImageMagick `compare -metric AE`) contra el PNG esperado.

Resultado: **0 PASS / 25 FAIL / 8 SKIP**.

Los 8 SKIP son ROMs sin PNG esperado en `vendor/` (`*_change2` y
`win_without_bg`); no cuentan como fallo.

Para reproducir y comparar:

```sh
make -C mts
QUIET=1 REPORT=/tmp/qtrebg_frames/now.tsv ./mts/compare_ppu_frames.sh
cut -f1-3 /tmp/qtrebg_frames/now.tsv > /tmp/qtrebg_frames/now-summary.tsv
diff -u documentation/baselines/ppu-v2-baseline-2026-11-12.tsv \
        /tmp/qtrebg_frames/now-summary.tsv
```

El segundo comando recorta las columnas portables; el report completo incluye
rutas absolutas de las ROMs y de los PGM.
