#!/usr/bin/env bash
#
# run_benchmarks_secuencial_O3.sh
#
# Compila la version SECUENCIAL (sin hilos ni procesos) con la bandera -O3
# y repite el mismo protocolo de pruebas que run_benchmarks.sh:
#   10 repeticiones x tamaños (600, 1200, 2400, 4800),
#   valores 0..9, semilla 155, verificacion activada.
#
# Salida: resultados_O3_secuencial_<fecha>.txt

set -euo pipefail

# Eleva la prioridad de este script y de todos sus hijos (gcc, matmul)
# para que procesos externos no roben tiempo de CPU durante las mediciones.
sudo -n renice -n -10 -p $$ >/dev/null 2>&1 || true

# Uso: ./run_benchmarks_secuencial_O3.sh [corrida_inicial] [archivo_resultados]
#   corrida_inicial : primer numero de corrida a ejecutar (1..40)  [def: 1]
#   archivo_resultados : archivo al que agregar (reanudacion)      [def: nuevo con fecha]
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
src="$script_dir/Matriz secuencial/matriz_secuencial.c"
matmul_bin="$script_dir/Matriz secuencial/matmul_secuencial_O3"

if [[ ! -f "$src" ]]; then
    printf 'Error: no se encontro el fuente en %s\n' "$src" >&2
    exit 1
fi

if [[ ! -x "$matmul_bin" || "$src" -nt "$matmul_bin" ]]; then
    gcc -O3 "$src" -o "$matmul_bin"
fi

if [[ ! -x "$matmul_bin" ]]; then
    printf 'Error: no se encontro un ejecutable en %s\n' "$matmul_bin" >&2
    exit 1
fi

start_run=${1:-1}
results_file=${2:-}

if (( start_run > 1 )); then
    # Reanudacion: agregar al archivo existente sin reescribir lo ya medido
    if [[ -z "$results_file" ]]; then
        results_file=$(ls -t "$script_dir"/resultados_O3_secuencial_*.txt 2>/dev/null | head -1 || true)
    fi
    if [[ -z "$results_file" || ! -f "$results_file" ]]; then
        printf 'Error: para reanudar desde la corrida %d se necesita un archivo de resultados existente\n' "$start_run" >&2
        exit 1
    fi
    print_header=0
    tee_args=("-a")
else
    if [[ -z "$results_file" ]]; then
        results_file="$script_dir/resultados_O3_secuencial_$(date +%Y%m%d_%H%M%S).txt"
    fi
    print_header=1
    tee_args=()
fi

sizes=(600 1200 2400 4800)

{
    if (( print_header )); then
        printf 'Version SECUENCIAL (sin hilos ni procesos) | Bandera: -O3 | Valores: 0..9 | Semilla: 155 | Verificacion: activada\n'
    fi
    execution=0

    for repetition in {1..10}; do
        for size in "${sizes[@]}"; do
            execution=$((execution + 1))
            if (( execution < start_run )); then
                continue
            fi
            printf '\n[%02d/40] Repeticion %d/10 - tamano %d - secuencial O3\n' \
                "$execution" "$repetition" "$size"
            "$matmul_bin" "$size" 9 1 155
        done
    done
} 2>&1 | tee "${tee_args[@]}" "$results_file"

printf '\nBenchmark secuencial O3 completado. Resultados guardados en: %s\n' "$results_file"
