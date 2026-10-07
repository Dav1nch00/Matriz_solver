#!/bin/bash
#
# comparar_transpuesta.sh - Laboratorio 2: version base (B por columnas) frente
# a version transpuesta (fila x fila), con las mismas banderas de GCC.
#
# Uso:
#   ./comparar_transpuesta.sh [repeticiones] [semilla] [base.c] [transpuesta.c]
#
#   repeticiones : corridas medidas por configuracion          [def: 5]
#   semilla      : semilla del generador aleatorio              [def: 42]
#   base.c       : version sin transponer                       [def: matmul_secuencial.c]
#   transpuesta.c: version con B transpuesta                    [def: matmul_secuencial_transpuesta.c]
#
# Variables de entorno opcionales:
#   TAMANOS="600 1000 1500"   tamaños de matriz a probar (cada uno >= 600)
#   BANDERAS="-O3|-O0"        conjuntos de banderas separados por |
#   NOTAS="conectado, alto rendimiento"   condiciones de la prueba (se guardan)
#
# Ejemplos:
#   ./comparar_transpuesta.sh 5 42 matriz_secuencial.c matriz_secuencial_transpuesta.c
#   TAMANOS="600 1000" BANDERAS="-O3" ./comparar_transpuesta.sh 5 42      # prueba rapida
#
# Salidas (en la carpeta actual):
#   info_sistema.txt                          procesador, cache, GCC, perf
#   resultados_transpuesta_individual.csv     cada corrida por separado
#   resultados_transpuesta_resumen.csv        mediana, minimo, maximo y mejora
#   resultados_perf.csv                       fallos de cache (solo si perf funciona)
#   bin_lab2/                                 ejecutables generados
#
# Metodologia:
#   - Misma semilla, mismas banderas y mismos tamaños para ambas versiones.
#   - Una corrida de calentamiento por version (se descarta).
#   - Las corridas se ALTERNAN (base, transpuesta, transpuesta, base, ...) para
#     que cambios de temperatura o de carga afecten a las dos por igual.
#   - El tiempo es el que reporta el programa: solo la multiplicacion.

REPS=${1:-5}
SEMILLA=${2:-42}
BASE=${3:-matmul_secuencial.c}
TRANS=${4:-matmul_secuencial_transpuesta.c}
TAMANOS=${TAMANOS:-"600 1000 1500"}
BANDERAS=${BANDERAS:-"-O3|-O0"}
NOTAS=${NOTAS:-"[completar: alimentacion, plan de energia, programas abiertos]"}
LIMITE=100

DIR_BIN="bin_lab2"
INFO="info_sistema.txt"
CSV_IND="resultados_transpuesta_individual.csv"
CSV_RES="resultados_transpuesta_resumen.csv"
CSV_PERF="resultados_perf.csv"

# ---- Comprobaciones previas --------------------------------------------------
if ! command -v gcc >/dev/null 2>&1; then
  echo "Error: gcc no esta instalado. En Debian: sudo apt install build-essential" >&2
  exit 1
fi
for f in "$BASE" "$TRANS"; do
  if [ ! -f "$f" ]; then
    echo "Error: no se encuentra el archivo fuente '$f'" >&2
    exit 1
  fi
done
if ! [[ "$REPS" =~ ^[0-9]+$ ]] || [ "$REPS" -lt 1 ]; then
  echo "Error: las repeticiones deben ser un entero >= 1 (recibido: $REPS)" >&2
  exit 1
fi
for n in $TAMANOS; do
  if ! [[ "$n" =~ ^[0-9]+$ ]] || [ "$n" -lt 600 ]; then
    echo "Error: cada tamaño debe ser un entero >= 600 (recibido: $n)" >&2
    exit 1
  fi
done

mkdir -p "$DIR_BIN"

# ---- Funciones auxiliares ----------------------------------------------------
mediana() {   # numeros por stdin, uno por linea
  sort -n | awk '{ v[NR]=$1 }
    END { if (NR==0) { print "NA"; exit }
          if (NR%2) printf "%.3f", v[(NR+1)/2];
          else      printf "%.3f", (v[NR/2]+v[NR/2+1])/2 }'
}
minimo() { sort -n | head -1; }
maximo() { sort -n | tail -1; }

# Ejecuta una vez: imprime "pared usuario sistema verif"
correr_una() {   # $1=ejecutable  $2=n  $3=verificar(0|1)
  local salida estado p u s v
  salida=$("$1" "$2" "$LIMITE" "$3" "$SEMILLA" 2>&1)
  estado=$?
  p=$(echo "$salida" | awk '/Tiempo de pared/{print $4}')
  u=$(echo "$salida" | awk '/Tiempo de usuario/{print $4}')
  s=$(echo "$salida" | awk '/Tiempo de sistema/{print $4}')
  v="OK"
  if [ "$3" = "1" ]; then
    if [ $estado -ne 0 ] || ! echo "$salida" | grep -q "Verificacion: OK"; then v="FALLO"; fi
  fi
  if [ -z "$p" ] || [ -z "$u" ] || [ -z "$s" ]; then v="SIN_DATOS"; p="NA"; u="NA"; s="NA"; fi
  echo "$p $u $s $v"
}

# perf: devuelve "referencias fallos" o "NA NA". Suma los contadores de todos
# los tipos de nucleo (en CPU hibridas perf reporta cpu_core y cpu_atom).
perf_medir() {   # $1=ejecutable $2=n
  perf stat -x, -e cache-references,cache-misses "$1" "$2" "$LIMITE" 0 "$SEMILLA" 2>&1 >/dev/null | awk -F, '
    $3 ~ /cache-references/ && $1 ~ /^[0-9]+$/ { r += $1; okr = 1 }
    $3 ~ /cache-misses/     && $1 ~ /^[0-9]+$/ { m += $1; okm = 1 }
    END { if (okr && okm) printf "%.0f %.0f", r, m; else print "NA NA" }'
}

# ---- Informacion del sistema -------------------------------------------------
{
  echo "=== Informacion del sistema (laboratorio 2) ==="
  echo "Fecha          : $(date '+%Y-%m-%d %H:%M:%S')"
  echo "GCC            : $(gcc --version | head -1)"
  echo "Kernel         : $(uname -r)"
  if grep -qi microsoft /proc/version 2>/dev/null; then echo "Entorno        : WSL"; else echo "Entorno        : Linux nativo"; fi
  echo "CPU            : $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2 | sed 's/^ //')"
  echo "Nucleos (nproc): $(nproc 2>/dev/null)"
  echo
  echo "--- Cache segun getconf (0 o vacio = el sistema no lo informa) ---"
  for v in LEVEL1_DCACHE_LINESIZE LEVEL1_DCACHE_SIZE LEVEL2_CACHE_SIZE LEVEL3_CACHE_SIZE; do
    printf "%-26s %s\n" "$v" "$(getconf $v 2>/dev/null)"
  done
  echo
  echo "--- lscpu (lineas relevantes) ---"
  lscpu 2>/dev/null | grep -i -E "model name|^CPU\(s\)|thread|core|socket|L1d|L1i|L2|L3|cache" || echo "lscpu no disponible"
  echo
  echo "--- Parametros de la prueba ---"
  echo "Tamaños        : $TAMANOS"
  echo "Banderas       : $BANDERAS"
  echo "Repeticiones   : $REPS | Semilla: $SEMILLA | Limite: $LIMITE"
  echo "Base           : $BASE"
  echo "Transpuesta    : $TRANS"
  echo "Notas          : $NOTAS"
} > "$INFO"

LINEA=$(getconf LEVEL1_DCACHE_LINESIZE 2>/dev/null)

# ---- Disponibilidad de perf --------------------------------------------------
PERF_OK=0
if command -v perf >/dev/null 2>&1; then
  res=$(perf stat -x, -e cache-references,cache-misses true 2>&1 >/dev/null | awk -F, '
    $3 ~ /cache-references/ && $1 ~ /^[0-9]+$/ { okr = 1 }
    $3 ~ /cache-misses/     && $1 ~ /^[0-9]+$/ { okm = 1 }
    END { if (okr && okm) print "si"; else print "no" }')
  if [ "$res" = "si" ]; then PERF_OK=1; fi
fi
if [ $PERF_OK -eq 1 ]; then
  echo "perf           : disponible (se registraran fallos de cache)" | tee -a "$INFO"
else
  echo "perf           : NO disponible o sin contadores de hardware (se omite)" | tee -a "$INFO"
fi

# ---- Encabezado en pantalla --------------------------------------------------
echo
echo "=============================================================="
echo " Laboratorio 2: base (B por columnas) vs transpuesta (fila x fila)"
echo "=============================================================="
echo "CPU        : $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2 | sed 's/^ //')"
echo "Linea cache: ${LINEA:-desconocida} bytes (getconf)   | GCC: $(gcc --version | head -1)"
echo "Tamaños    : $TAMANOS | Banderas: $BANDERAS | Repeticiones: $REPS | Semilla: $SEMILLA"
echo "Detalle del sistema guardado en: $INFO"
echo "Consejo    : conecte el portatil, cierre programas pesados y no use el equipo durante la prueba."
echo

echo "banderas,n,version,corrida,pared_s,usuario_s,sistema_s,verificacion" > "$CSV_IND"
echo "banderas,n,mediana_base_s,min_base_s,max_base_s,brecha_base_pct,mediana_transp_s,min_transp_s,max_transp_s,brecha_transp_pct,mejora_mediana,verif_base,verif_transp" > "$CSV_RES"
if [ $PERF_OK -eq 1 ]; then
  echo "banderas,n,version,cache_references,cache_misses,tasa_fallos_pct" > "$CSV_PERF"
fi

declare -a FILAS=()
declare -a FILAS_PERF=()

IFS='|' read -r -a LISTA_BANDERAS <<< "$BANDERAS"

for fl in "${LISTA_BANDERAS[@]}"; do
  etiqueta=$(echo "$fl" | tr -d ' -')
  exe_b="$DIR_BIN/base_$etiqueta"
  exe_t="$DIR_BIN/transp_$etiqueta"

  # shellcheck disable=SC2086
  if ! gcc $fl "$BASE" -o "$exe_b" 2>/dev/null; then echo "ERROR compilando $BASE con $fl"; continue; fi
  # shellcheck disable=SC2086
  if ! gcc $fl "$TRANS" -o "$exe_t" 2>/dev/null; then echo "ERROR compilando $TRANS con $fl"; continue; fi

  for n in $TAMANOS; do
    echo -n "[$fl | n=$n] calentando y midiendo ... "
    correr_una "$exe_b" "$n" 0 >/dev/null
    correr_una "$exe_t" "$n" 0 >/dev/null

    pb=""; pt=""; vb="OK"; vt="OK"
    for ((r = 1; r <= REPS; r++)); do
      # Alternar el orden para no favorecer sistematicamente a una version
      if (( r % 2 == 1 )); then orden="base transp"; else orden="transp base"; fi
      for ver in $orden; do
        if [ "$ver" = "base" ]; then exe="$exe_b"; else exe="$exe_t"; fi
        read -r p u s v <<< "$(correr_una "$exe" "$n" 1)"
        echo "$fl,$n,$ver,$r,$p,$u,$s,$v" >> "$CSV_IND"
        if [ "$ver" = "base" ]; then
          pb+="$p"$'\n'; [ "$v" != "OK" ] && vb="$v"
        else
          pt+="$p"$'\n'; [ "$v" != "OK" ] && vt="$v"
        fi
      done
    done

    mb=$(printf "%s" "$pb" | mediana); mnb=$(printf "%s" "$pb" | minimo); mxb=$(printf "%s" "$pb" | maximo)
    mt=$(printf "%s" "$pt" | mediana); mnt=$(printf "%s" "$pt" | minimo); mxt=$(printf "%s" "$pt" | maximo)
    gb=$(awk -v m="$mb" -v n="$mnb" 'BEGIN{ if (m>0) printf "%.1f", (m-n)/m*100; else print "NA" }')
    gt=$(awk -v m="$mt" -v n="$mnt" 'BEGIN{ if (m>0) printf "%.1f", (m-n)/m*100; else print "NA" }')
    mej=$(awk -v b="$mb" -v t="$mt" 'BEGIN{ if (t>0) printf "%.2f", b/t; else print "NA" }')

    echo "$fl,$n,$mb,$mnb,$mxb,$gb,$mt,$mnt,$mxt,$gt,$mej,$vb,$vt" >> "$CSV_RES"
    FILAS+=("$fl|$n|$mb|$mnb|$gb|$mt|$mnt|$gt|$mej|$vb/$vt")
    echo "base=${mb}s transpuesta=${mt}s mejora=${mej}x"

    # perf (una corrida por version; cuenta el proceso completo, no solo la multiplicacion)
    if [ $PERF_OK -eq 1 ]; then
      for ver in base transp; do
        if [ "$ver" = "base" ]; then exe="$exe_b"; else exe="$exe_t"; fi
        read -r refs miss <<< "$(perf_medir "$exe" "$n")"
        tasa=$(awk -v r="$refs" -v m="$miss" 'BEGIN{ if (r+0>0) printf "%.2f", m/r*100; else print "NA" }')
        echo "$fl,$n,$ver,$refs,$miss,$tasa" >> "$CSV_PERF"
        FILAS_PERF+=("$fl|$n|$ver|$refs|$miss|$tasa")
      done
    fi
  done
done

# ---- Tabla final -------------------------------------------------------------
echo
echo "=============================================================="
echo " RESULTADOS (tiempo de pared de la multiplicacion, $REPS corridas)"
echo "=============================================================="
printf "%-7s %-5s | %9s %8s %7s | %9s %8s %7s | %8s | %s\n" \
  "Flags" "n" "Base med" "min" "brecha" "Transp med" "min" "brecha" "Mejora" "Verif"
printf "%-7s %-5s-+-%9s-%8s-%7s-+-%10s-%8s-%7s-+-%8s-+-%s\n" \
  "-------" "-----" "---------" "--------" "-------" "----------" "--------" "-------" "--------" "------"
for fila in "${FILAS[@]}"; do
  IFS='|' read -r fl n mb mnb gb mt mnt gt mej vv <<< "$fila"
  printf "%-7s %-5s | %9s %8s %6s%% | %10s %8s %6s%% | %7sx | %s\n" \
    "$fl" "$n" "$mb" "$mnb" "$gb" "$mt" "$mnt" "$gt" "$mej" "$vv"
done
echo
echo "Mejora = mediana(base) / mediana(transpuesta). Mayor que 1 = la transpuesta es mas rapida."
echo "Brecha = (mediana - minimo) / mediana: indica que tan estables fueron las corridas."

if [ $PERF_OK -eq 1 ]; then
  echo
  echo "=============================================================="
  echo " FALLOS DE CACHE (perf; cuenta el proceso completo)"
  echo "=============================================================="
  printf "%-7s %-5s %-8s %16s %16s %10s\n" "Flags" "n" "Version" "cache-refs" "cache-misses" "Tasa"
  for fila in "${FILAS_PERF[@]}"; do
    IFS='|' read -r fl n ver refs miss tasa <<< "$fila"
    printf "%-7s %-5s %-8s %16s %16s %9s%%\n" "$fl" "$n" "$ver" "$refs" "$miss" "$tasa"
  done
fi

echo
echo "Archivos generados:"
echo "  $INFO"
echo "  $CSV_IND   (tiempos de cada corrida)"
echo "  $CSV_RES   (resumen)"
if [ $PERF_OK -eq 1 ]; then echo "  $CSV_PERF"; fi
