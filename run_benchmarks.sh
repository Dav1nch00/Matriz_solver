#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
matmul_bin=${MATMUL_BIN:-"$script_dir/matmul"}

gcc -O2 -pthread "$script_dir/matmul.c" -o "$matmul_bin"

if [[ ! -x "$matmul_bin" ]]; then
    printf 'Error: no se encontro un ejecutable en %s\n' "$matmul_bin" >&2
    exit 1
fi

read -r -p 'Numero de hilos: ' threads
if [[ ! "$threads" =~ ^[1-9][0-9]*$ ]]; then
    printf 'Error: introduce un numero entero de hilos mayor que cero.\n' >&2
    exit 1
fi

sizes=(600 1200 2400 4800)
reps=(10 10 10 3)
limite=9
semilla=155

results_file=${RESULTS_FILE:-"$script_dir/resultados_$(date +%Y%m%d_%H%M%S).txt"}
raw=$(mktemp)
trap 'rm -f "$raw"' EXIT

{
    printf 'Hilos: %s | Valores: 0..%s | Semilla: %s | Verificacion: activada\n' \
        "$threads" "$limite" "$semilla"
    for idx in "${!sizes[@]}"; do
        size=${sizes[$idx]}
        total=${reps[$idx]}
        for ((repeticion = 1; repeticion <= total; repeticion++)); do
            printf '\n[%02d/%02d] tamano %d - repeticion %d/%d - hilos %s\n' \
                "$repeticion" "$total" "$size" "$repeticion" "$total" "$threads"
            "$matmul_bin" "$size" "$limite" 1 "$semilla" "$threads"
        done
    done
} 2>&1 | tee "$results_file" "$raw" >/dev/null

printf '\n===== Resumen (media +- desviacion) =====\n'
awk -v hilos="$threads" '
function flush(   mp, mu, vp, der) {
    if (np == 0) return
    mp = sp / np
    mu = su / np
    vp = (np > 1) ? sqrt((sp2 - sp * sp / np) / (np - 1)) : 0
    der = (first > 0) ? (lastp - first) * 100 / first : 0
    printf "  n=%-6d reps=%-3d pared %8.3f +- %-8.3f usuario %8.3f +- %-8.3f sistema %6.3f | min/max %8.3f /%8.3f | deriva %+.1f%%\n", \
        n, np, mp, vp, mu, vp, ss / np, minp, maxp, der
    sp = sp2 = su = ss = 0
    np = 0
    first = minp = maxp = lastp = ""
}
BEGIN {
    printf "\nHilos: %s\n", hilos
    printf "%-9s %-6s %-21s %-21s %-11s %s\n", "n", "reps", "pared media +- sd", "usuario media +- sd", "sistema media", "rango pared / deriva"
}
/tamano/ {
    for (i = 1; i <= NF; i++) if ($i == "tamano") newn = $(i + 1)
    if (np > 0 && newn != n) flush()
    n = newn
}
/Tiempo de pared/   { for (i = 1; i <= NF; i++) if ($i == "pared:")   v = $(i + 1) }
/Tiempo de usuario/ { for (i = 1; i <= NF; i++) if ($i == "usuario:") u = $(i + 1) }
/Tiempo de sistema/ { for (i = 1; i <= NF; i++) if ($i == "sistema:") s = $(i + 1) }
v != "" {
    np++; sp += v; sp2 += v * v; su += u; ss += s
    if (np == 1) { first = v; minp = v; maxp = v }
    if (v < minp) minp = v
    if (v > maxp) maxp = v
    lastp = v
    v = ""; u = ""; s = ""
}
END { flush() }
' "$raw" | tee -a "$results_file"


printf '\nResultados guardados en: %s\n' "$results_file"
