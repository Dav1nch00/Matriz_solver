set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
matmul_source="$script_dir/matmul_openmp_transpuesta.c"
matmul_bin=${MATMUL_BIN:-"$script_dir/matmul_openmp_transpuesta"}
limit=${LIMIT:-100}
seed=${SEED:-42}
threads=(1 2 3 4 5)
sizes=(600 1200 2400 4800)
repetitions=10

if [[ -z "${MATMUL_BIN:-}" ]] && [[ ! -x "$matmul_bin" || "$matmul_source" -nt "$matmul_bin" ]]; then
    gcc -O3 -fopenmp "$matmul_source" -o "$matmul_bin"
fi

if [[ ! -x "$matmul_bin" ]]; then
    printf 'Error: no se encontro un ejecutable en %s\n' "$matmul_bin" >&2
    exit 1
fi

results_base=${RESULTS_FILE:-"$script_dir/resultados$(date +%Y%m%d_%H%M%S)"}
results_base=${results_base%.txt}
executions_per_thread=$((${#sizes[@]} * repetitions))

for thread_count in "${threads[@]}"; do
    thread_execution=0
    results_file="${results_base}_hilos_${thread_count}.txt"
    {
        printf 'Version: OpenMP transpuesta | Optimizacion: -O3 | Hilos: %s | Valores: 0..%s | Semilla: %s | Verificacion: activada\n' \
            "$thread_count" "$limit" "$seed"
        for repetition in $(seq 1 "$repetitions"); do
            for size in "${sizes[@]}"; do
                ((thread_execution += 1))
                printf '\n[%02d/%02d] Repeticion %s/%s - tamano %s\n' \
                    "$thread_execution" "$executions_per_thread" "$repetition" \
                    "$repetitions" "$size"
                "$matmul_bin" "$size" "$limit" 1 "$seed" "$thread_count"
            done
        done
    } 2>&1 | tee "$results_file"

    printf '\nResultados del hilo %s guardados en: %s\n' "$thread_count" "$results_file"
done