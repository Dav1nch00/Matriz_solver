#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
matmul_bin=${MATMUL_BIN:-"$script_dir/matmul"}

if [[ -z "${MATMUL_BIN:-}" ]] && [[ ! -x "$matmul_bin" || "$script_dir/matmul.c" -nt "$matmul_bin" ]]; then
    gcc -O2 -pthread "$script_dir/matmul.c" -o "$matmul_bin"
fi

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
execution=0
results_file=${RESULTS_FILE:-"$script_dir/resultados$(date +%Y%m%d_%H%M%S).txt"}

{
    printf 'Hilos: %s | Valores: 0..9 | Semilla: 155 | Verificacion: activada\n' "$threads"
    for repetition in {1..10}; do
        for size in "${sizes[@]}"; do
            ((execution += 1))
            printf '\n[%02d/40] Repeticion %d/10 - tamano %d - hilos %s\n' \
                "$execution" "$repetition" "$size" "$threads"
            "$matmul_bin" "$size" 9 1 155 "$threads"
        done
    done
} 2>&1 | tee "$results_file"

printf '\nResultados guardados en: %s\n' "$results_file"