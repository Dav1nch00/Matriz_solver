#!/bin/bash
#
# comparar_flags.sh - Compila matmul_secuencial.c con distintas banderas de
# optimización de GCC, ejecuta cada variante varias veces y resume los tiempos.
#
# Uso:
#   ./comparar_flags.sh [tamaño] [repeticiones] [semilla] [fuente.c]
#
#   tamaño        : dimensión N de las matrices (mínimo 600)   [def: 1000]
#   repeticiones  : corridas medidas por variante               [def: 5]
#   semilla       : semilla del generador aleatorio             [def: 42]
#   fuente.c      : programa a compilar                         [def: matmul_secuencial.c]
#
# Ejemplo:
#   chmod +x comparar_flags.sh
#   ./comparar_flags.sh 1000 5 42
#
# Salidas:
#   - Tabla en pantalla
#   - resultados_flags.csv (para graficar en Excel o Python)
#   - Ejecutables en la carpeta ./bin_flags/

N=${1:-1000}
REPS=${2:-5}
SEMILLA=${3:-42}
FUENTE=${4:-matmul_secuencial.c}
LIMITE=100
CSV="resultados_flags.csv"
DIR_BIN="bin_flags"

# ---- Variantes a comparar: "nombre|banderas" ---------------------------------
# La primera (O0) es la línea base para calcular el speedup.
VARIANTES=(
  "O0|-O0"
  "O1|-O1"
  "O2|-O2"
  "O3|-O3"
  "O3_unroll|-O3 -funroll-loops"
  "O3_native_unroll|-O3 -march=native -funroll-loops"
)

# ---- Comprobaciones previas --------------------------------------------------
if ! command -v gcc >/dev/null 2>&1; then
  echo "Error: gcc no esta instalado. En Debian: sudo apt install build-essential" >&2
  exit 1
fi
if [ ! -f "$FUENTE" ]; then
  echo "Error: no se encuentra el archivo fuente '$FUENTE'" >&2
  exit 1
fi
if ! [[ "$N" =~ ^[0-9]+$ ]] || [ "$N" -lt 600 ]; then
  echo "Error: el tamaño debe ser un entero >= 600 (recibido: $N)" >&2
  exit 1
fi
if ! [[ "$REPS" =~ ^[0-9]+$ ]] || [ "$REPS" -lt 1 ]; then
  echo "Error: las repeticiones deben ser un entero >= 1 (recibido: $REPS)" >&2
  exit 1
fi

mkdir -p "$DIR_BIN"

# Mediana de una lista de numeros (uno por linea) recibida por stdin
mediana() {
  sort -n | awk '{ v[NR]=$1 }
    END { if (NR==0) { print "NA"; exit }
          if (NR%2) printf "%.3f", v[(NR+1)/2];
          else      printf "%.3f", (v[NR/2]+v[NR/2+1])/2 }'
}

# ---- Informacion del entorno -------------------------------------------------
echo "=============================================================="
echo " Comparacion de banderas de GCC - $FUENTE"
echo "=============================================================="
echo "GCC        : $(gcc --version | head -1)"
echo "CPU        : $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2 | sed 's/^ //')"
echo "Nucleos    : $(nproc 2>/dev/null)"
echo "Tamaño N   : $N  | Repeticiones: $REPS | Semilla: $SEMILLA"
echo "Consejo    : cierre programas pesados y conecte el portatil a la corriente."
echo

echo "variante,banderas,mediana_pared_s,min_pared_s,mediana_usuario_s,mediana_sistema_s,speedup_vs_O0,verificacion" > "$CSV"

BASE_PARED=""
declare -a FILAS=()

for item in "${VARIANTES[@]}"; do
  nombre="${item%%|*}"
  banderas="${item#*|}"
  exe="$DIR_BIN/matmul_$nombre"

  # Compilar
  # shellcheck disable=SC2086
  if ! gcc $banderas "$FUENTE" -o "$exe" 2>/dev/null; then
    echo "[$nombre] ERROR al compilar con: $banderas"
    FILAS+=("$nombre|$banderas|ERROR|ERROR|ERROR|ERROR|ERROR|ERROR")
    echo "$nombre,\"$banderas\",ERROR,ERROR,ERROR,ERROR,ERROR,ERROR" >> "$CSV"
    continue
  fi

  echo -n "[$nombre] $banderas ... "

  # Corrida de calentamiento (se descarta)
  "$exe" "$N" "$LIMITE" 0 "$SEMILLA" >/dev/null 2>&1

  paredes=""; usuarios=""; sistemas=""; verif="OK"
  for ((r = 1; r <= REPS; r++)); do
    salida=$("$exe" "$N" "$LIMITE" 1 "$SEMILLA" 2>&1)
    estado=$?
    p=$(echo "$salida" | awk '/Tiempo de pared/{print $4}')
    u=$(echo "$salida" | awk '/Tiempo de usuario/{print $4}')
    s=$(echo "$salida" | awk '/Tiempo de sistema/{print $4}')
    if [ $estado -ne 0 ] || ! echo "$salida" | grep -q "Verificacion: OK"; then
      verif="FALLO"
    fi
    paredes+="$p"$'\n'; usuarios+="$u"$'\n'; sistemas+="$s"$'\n'
  done

  med_p=$(printf "%s" "$paredes"  | mediana)
  min_p=$(printf "%s" "$paredes"  | sort -n | head -1)
  med_u=$(printf "%s" "$usuarios" | mediana)
  med_s=$(printf "%s" "$sistemas" | mediana)

  if [ -z "$BASE_PARED" ]; then BASE_PARED="$med_p"; fi
  speed=$(awk -v b="$BASE_PARED" -v t="$med_p" 'BEGIN{ if (t>0) printf "%.2f", b/t; else print "NA" }')

  echo "pared (mediana) = ${med_p}s"
  FILAS+=("$nombre|$banderas|$med_p|$min_p|$med_u|$med_s|$speed|$verif")
  echo "$nombre,\"$banderas\",$med_p,$min_p,$med_u,$med_s,$speed,$verif" >> "$CSV"
done

# ---- Tabla final -------------------------------------------------------------
echo
echo "=============================================================="
echo " RESULTADOS (n=$N, mediana de $REPS corridas)"
echo "=============================================================="
printf "%-18s %-34s %9s %9s %9s %8s %8s %6s\n" \
  "Variante" "Banderas" "Pared(s)" "Min(s)" "Usuario" "Sistema" "Speedup" "Verif"
printf "%-18s %-34s %9s %9s %9s %8s %8s %6s\n" \
  "------------------" "----------------------------------" "---------" "---------" "---------" "--------" "--------" "------"
for fila in "${FILAS[@]}"; do
  IFS='|' read -r nom ban p mn u s sp v <<< "$fila"
  printf "%-18s %-34s %9s %9s %9s %8s %8s %6s\n" "$nom" "$ban" "$p" "$mn" "$u" "$s" "$sp" "$v"
done
echo
echo "Speedup = pared(-O0) / pared(variante). Mayor que 1 = mas rapido que -O0."
echo "Datos guardados en: $CSV"
