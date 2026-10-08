#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
matmul_bin=${MATMUL_BIN:-"$script_dir/matmul_procesos"}

if [[ -z "${MATMUL_BIN:-}" ]] && [[ ! -x "$matmul_bin" || "$script_dir/matmul_procesos.c" -nt "$matmul_bin" ]]; then
    gcc -O2 "$script_dir/matmul_procesos.c" -o "$matmul_bin"
fi

if [[ ! -x "$matmul_bin" ]]; then
    printf 'Error: no se encontro un ejecutable en %s\n' "$matmul_bin" >&2
    exit 1
fi

sizes=(600 1200 2400 4800)

run_benchmark() {
    local processes="$1"
    local results_file="$2"
    local execution=0

    {
        printf 'Procesos: %s | Valores: 0..9 | Semilla: 155 | Verificacion: activada\n' "$processes"
        for repetition in {1..10}; do
            for size in "${sizes[@]}"; do
                ((execution += 1))
                printf '\n[%02d/40] Repeticion %d/10 - tamano %d - procesos %s\n' \
                    "$execution" "$repetition" "$size" "$processes"
                "$matmul_bin" "$size" 9 1 155 "$processes"
            done
        done
    } 2>&1 | tee "$results_file"
}

for processes in 12 8 4; do ##se puede cambiar el numero de procesos segun se desee
    results_file="$script_dir/resultados_${processes}procesos_$(date +%Y%m%d_%H%M%S).txt"
    printf '\n=== Ejecutando benchmark con %s procesos ===\n' "$processes"
    run_benchmark "$processes" "$results_file"
    printf '\nResultados para %s procesos guardados en: %s\n' "$processes" "$results_file"
done

printf '\nBenchmarks completados. Se generaron archivos separados para 12 procesos y 8 procesos.\n'