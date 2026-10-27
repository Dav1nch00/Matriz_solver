# Laboratorio 2: optimización secuencial mediante transposición de matriz

## 1. Resumen

En este laboratorio se comparó la multiplicación secuencial clásica de matrices con una variante que almacena la segunda matriz transpuesta en memoria. El objetivo fue evaluar si cambiar el patrón de acceso a los datos mejora el tiempo de cálculo, manteniendo el mismo problema matemático y el mismo algoritmo de multiplicación.

Para matrices de `1000 × 1000`, compiladas ambas versiones con GCC y `-O3`, se midieron cinco ejecuciones de cada versión. La mediana de la versión base fue **0.990 s** y la mediana de la versión transpuesta fue **0.391 s**. La razón entre ambas medianas es **2.53×**: en las condiciones observadas, la versión transpuesta tardó alrededor del **39.5 %** del tiempo base, una reducción de tiempo aproximada del **60.5 %**.

Las cinco ejecuciones transpuestas fueron más rápidas que sus correspondientes mediciones base y todas las verificaciones reportaron `OK`. Este resultado respalda la utilidad de mejorar la localidad de memoria para este caso; no demuestra una aceleración universal ni un beneficio del paralelismo, ya que las dos versiones son secuenciales.

## 2. Objetivo y pregunta experimental

La pregunta del experimento fue:

> ¿Qué efecto tiene almacenar `B` como su transpuesta sobre el tiempo de la multiplicación secuencial `C = A × B`, manteniendo iguales el tamaño, los datos lógicos, las opciones de compilación y las condiciones de ejecución?

La variable modificada es la disposición de `B` en memoria y, en consecuencia, el acceso a sus elementos dentro del ciclo más interno. El trabajo matemático sigue siendo la multiplicación clásica de matrices.

## 3. Archivos y función de cada uno

- `matriz_secuencial.c`: implementación base. Al calcular `C[i][j]`, lee `A[i][k]` por una fila y `B[k][j]` por una columna.
- `matriz_secuencial_transpuesta.c`: implementación optimizada en localidad. Guarda los valores lógicos de `B` en una matriz `BT` de modo que `BT[j][k] = B[k][j]`; el producto punto recorre una fila de `A` y una fila de `BT`.
- `comparar_transpuesta.sh`: compila ambas fuentes con las mismas banderas, hace calentamientos, alterna el orden de las versiones durante las repeticiones, registra tiempos y genera los CSV y el archivo de información del sistema.
- `resultados_transpuesta_individual.csv`: medición por ejecución, con tiempos de pared, usuario y sistema, y estado de verificación.
- `resultados_transpuesta_resumen.csv`: medianas, extremos, brecha y razón de mejora calculados por el script.
- `info_sistema.txt`: entorno, compilador, procesador, cachés y parámetros de la prueba.

## 4. Cómo funciona cada versión

### 4.1 Multiplicación base

Para cada elemento de la matriz resultado, la versión base calcula:

```text
C[i][j] = suma para k = 0..N-1 de A[i][k] * B[k][j]
```

Las matrices se reservan como bloques contiguos en memoria, con sus filas contiguas (orden por filas, o *row-major*). Por eso, `A[i][k]` avanza secuencialmente al aumentar `k`, pero `B[k][j]` salta de una fila a la siguiente. Para `N = 1000` y elementos de 8 bytes, ese salto entre elementos de la misma columna es de aproximadamente 8.000 bytes.

### 4.2 Multiplicación con `B` transpuesta

La variante transpuesta genera los mismos valores lógicos de `B`, en el mismo orden de llamadas al generador aleatorio, pero escribe cada valor `B[i][j]` en `BT[j][i]`. Así, la operación puede escribirse como:

```text
C[i][j] = suma para k = 0..N-1 de A[i][k] * BT[j][k]
```

Ahora ambos operandos del producto punto se leen en posiciones contiguas. Esto favorece la localidad espacial: cuando el procesador carga una línea de caché, los siguientes elementos que necesita suelen estar cerca y pueden venir en esa misma línea o en líneas consecutivas.

La transposición no crea una cuarta matriz ni agrega espacio de almacenamiento respecto de la matriz `B` original: `BT` ocupa el lugar de `B`. La escritura transpuesta se hace durante la generación de datos, antes de iniciar el cronómetro de la multiplicación.

### 4.3 Complejidad y trabajo matemático

Las dos versiones usan tres ciclos anidados y conservan complejidad temporal `O(N³)`. Para `N = 1000`, el orden de magnitud es:

- `N³ = 1.000.000.000` multiplicaciones.
- `N² × (N − 1) = 999.000.000` sumas para formar los productos punto.
- Aproximadamente 1.999 millones de operaciones aritméticas escalares si se cuentan por separado multiplicaciones y sumas.

La versión transpuesta no reduce esa cantidad de operaciones; busca que el acceso a los operandos sea más eficiente.

## 5. Entorno y configuración

Según `info_sistema.txt`, la prueba se realizó con:

| Parámetro | Valor |
|---|---|
| Fecha registrada | 2026-10-07 17:15:40 |
| Entorno | WSL 2 |
| Kernel | `6.18.33.2-microsoft-standard-WSL2` |
| Compilador | GCC 14.2.0 (Debian 14.2.0-19) |
| Procesador reportado | Intel Core i5-1334U de 13.ª generación |
| Procesadores lógicos visibles | 12 |
| Tamaño de matriz | `1000 × 1000` |
| Banderas | `-O3` para ambas versiones |
| Repeticiones medidas | 5 por versión |
| Semilla | 42 |
| Rango aleatorio | 0 a 100, inclusive |
| `perf` | No disponible o sin contadores de hardware; no se registraron contadores |

La información de caché reportada por el sistema incluye líneas de 64 bytes, L1 de datos de 48 KiB por instancia, L2 de 1.25 MiB por instancia y L3 de 12 MiB. En `lscpu` aparecen los tamaños agregados de las instancias: 288 KiB de L1d en seis instancias y 7.5 MiB de L2 en seis instancias, además de 12 MiB de L3.

Tres matrices de `1000 × 1000` elementos `long long` ocupan alrededor de 24 MB de datos (sin contar las pequeñas tablas de punteros y otros gastos). Este volumen es mayor que las cachés indicadas, por lo que el patrón de lectura puede tener un efecto importante; los archivos del experimento, sin embargo, no miden directamente los fallos de caché.

El campo `Notas` del archivo de sistema conserva el texto `[completar: alimentacion, plan de energia, programas abiertos]`. Por tanto, no quedó registrado si el portátil estuvo conectado a la corriente, qué plan de energía estaba activo o qué carga externa tenía el equipo.

## 6. Procedimiento ejecutado

El script `comparar_transpuesta.sh` sigue, en esencia, estos pasos:

1. Comprueba que GCC y los dos archivos fuente estén disponibles y valida tamaños y repeticiones.
2. Compila ambas versiones con el mismo conjunto de banderas. En esta ejecución se utilizó solo `-O3`.
3. Hace una corrida de calentamiento de cada versión y descarta sus tiempos.
4. Ejecuta cinco mediciones de cada versión. Alterna el orden por repetición: base y transpuesta en una, transpuesta y base en la siguiente.
5. Para cada ejecución invoca el programa con `N=1000`, límite `100`, verificación activada (`1`) y semilla `42`.
6. Lee los tiempos que informa cada programa. Estos cronómetros rodean la función de multiplicación; la reserva, el llenado de las matrices y la verificación quedan fuera de ese intervalo.
7. Comprueba que cada programa haya terminado correctamente y haya impreso `Verificacion: OK`.
8. Calcula medianas, mínimos, máximos, brecha y razón de mejora, y guarda resultados individuales y resumidos.

El orden alternado ayuda a reducir un posible sesgo por temperatura o carga que cambie con el paso del tiempo; no elimina todas las fuentes de variación. El calentamiento de ambas versiones siempre se hace primero en el orden indicado por el script.

## 7. Resultados individuales

Los tiempos siguientes son segundos de pared reportados por el programa:

| Repetición | Base | Transpuesta | Base más rápida |
|---:|---:|---:|---|
| 1 | 0.948 | 0.376 | No |
| 2 | 0.990 | 0.362 | No |
| 3 | 0.973 | 0.506 | No |
| 4 | 1.166 | 0.465 | No |
| 5 | 1.195 | 0.391 | No |

En cada una de las cinco repeticiones, la versión transpuesta fue más rápida. El orden del CSV alterna de acuerdo con la metodología: algunas filas de la versión transpuesta aparecen antes que las de la versión base.

### Resumen estadístico

| Medida | Base | Transpuesta |
|---|---:|---:|
| Mediana de pared | 0.990 s | 0.391 s |
| Mediana de usuario | 0.975 s | 0.391 s |
| Mediana de sistema | 0.016 s | 0.012 s |
| Mínimo | 0.948 s | 0.362 s |
| Máximo | 1.195 s | 0.506 s |
| Rango máximo-mínimo | 0.247 s | 0.144 s |
| Verificación | OK en las 5 corridas | OK en las 5 corridas |

La mediana de cinco valores es el valor central una vez ordenadas las mediciones:

```text
Base:        0.948, 0.973, 0.990, 1.166, 1.195  → mediana = 0.990 s
Transpuesta: 0.362, 0.376, 0.391, 0.465, 0.506  → mediana = 0.391 s
```

La mediana es preferible al mínimo para resumir estas pocas repeticiones porque es menos sensible a una sola ejecución inusualmente rápida.

## 8. Cálculos de rendimiento

### 8.1 Razón de mejora (*speedup*)

El script calcula:

```text
mejora = mediana_base / mediana_transpuesta
        = 0.990 / 0.391
        ≈ 2.53×
```

La interpretación correcta es que, según las medianas observadas, la versión transpuesta tardó aproximadamente `1 / 2.53`, o **39.5 %**, del tiempo de la base. No significa que el equipo haya usado 2.53 veces más núcleos ni que la multiplicación se haya paralelizado.

### 8.2 Reducción del tiempo

```text
reducción (%) = (mediana_base − mediana_transpuesta)
                / mediana_base × 100
              = (0.990 − 0.391) / 0.990 × 100
              ≈ 60.5 %
```

La diferencia absoluta entre medianas es de `0.599 s` por multiplicación para este tamaño y configuración.

### 8.3 Variación entre corridas y columna “brecha”

Las mediciones base van de 0.948 a 1.195 s; las transpuestas, de 0.362 a 0.506 s. La columna `brecha_*_pct` del resumen que genera el script se calcula como:

```text
brecha = (mediana − mínimo) / mediana × 100
```

Con esa fórmula, las cifras son 4.2 % para la base y 7.4 % para la transpuesta. Es una comparación entre la mediana y el mínimo, no una medida completa de dispersión ni el rango máximo-mínimo. El rango absoluto observado fue 0.247 s para la base y 0.144 s para la transpuesta. Como las escalas de tiempo son diferentes, para comparar variabilidad relativa también sería posible reportar el rango dividido por la mediana: aproximadamente 24.9 % para la base y 36.8 % para la transpuesta. Con solo cinco muestras, estas cifras son descriptivas, no una caracterización estadística concluyente.

## 9. Interpretación

### Lo que sí respaldan estos datos

1. **En la configuración ensayada, la transpuesta fue claramente más rápida.** La mejora mediana fue 2.53× y la transpuesta ganó en las cinco repeticiones.
2. **La explicación por localidad de memoria es coherente con el cambio de código.** La versión base accede a `B` por columnas en el bucle interno; la variante accede secuencialmente a las filas de `BT`. Este cambio puede reducir saltos de memoria y aprovechar mejor las líneas de caché.
3. **No se aumentó el almacenamiento de las matrices.** La matriz `BT` sustituye a `B`; las dos variantes trabajan con tres matrices del mismo tamaño.
4. **Las verificaciones pasaron.** Cada ejecución recomputó una muestra determinista de hasta 1.000 celdas y no encontró discrepancias.
5. **La mejora no se debe a una diferencia intencional en la bandera de compilación.** Ambas versiones se compilaron con la misma bandera, `-O3`, en el mismo experimento.

### Lo que no puede afirmarse solo con estos archivos

1. **No se midieron directamente los fallos de caché.** `perf` no estuvo disponible o no permitió acceder a los contadores. Por eso es razonable atribuir la mejora al patrón de memoria por la estructura del código, pero no se puede afirmar que el experimento haya demostrado una cantidad concreta de fallos de caché evitados.
2. **La verificación no es exhaustiva ni compara directamente las dos matrices `C`.** Cada programa vuelve a calcular hasta 1.000 celdas de su propio resultado. Un `OK` es evidencia útil contra errores en esa muestra, pero no una prueba formal de que todas las celdas sean correctas o idénticas.
3. **No se puede generalizar el factor 2.53× a todos los tamaños, procesadores o compiladores.** Solo se probó `N=1000`, en este entorno WSL, con GCC 14.2.0 y `-O3`.
4. **No se aisló toda posible variación del sistema.** El plan de energía, la alimentación y la carga externa no quedaron documentados; también pueden influir temperatura, frecuencia dinámica y actividad de WSL/Windows.
5. **No se comparó una versión paralela ni un algoritmo asintóticamente distinto.** La mejora corresponde a organización de memoria en el algoritmo secuencial `O(N³)`.

## 10. Observaciones sobre los CSV y el script

- `resultados_transpuesta_individual.csv` contiene el encabezado `banderas,n,version,corrida,pared_s,usuario_s,sistema_s,verificacion` y diez filas de datos: cinco por versión.
- `resultados_transpuesta_resumen.csv` contiene las medianas, extremos, las brechas calculadas por el script, la mejora mediana y el estado de verificación.
- `info_sistema.txt` indica que no se produjo `resultados_perf.csv`, porque los contadores `perf` no estuvieron disponibles.
- El tiempo de usuario suele estar cerca del tiempo de pared y el tiempo de sistema es pequeño, consistente con un trabajo de cálculo ejecutado por un único proceso. Estos números por sí solos no son un contador de núcleos activos ni una medición de paralelismo.
- Las medianas de CPU de usuario y de pared son cercanas (base: 0.975 s y 0.990 s; transpuesta: 0.391 s y 0.391 s), mientras que el tiempo de sistema es pequeño (0.016 s y 0.012 s). Es esperable en una carga dominada por cálculo de usuario; pequeñas diferencias entre reloj de pared y CPU pueden venir de planificación del sistema y resolución/medición de los relojes.
- La bandera `-O3` es la configuración elegida en el laboratorio anterior; este laboratorio compara el efecto de transponer bajo esa configuración, no vuelve a comparar `-O0`, `-O1`, `-O2` y `-O3`.

## 11. Conclusión

El laboratorio implementó y midió una optimización de localidad de memoria en la multiplicación secuencial de matrices. La versión base recorre la segunda matriz por columnas durante el producto punto; la versión transpuesta representa esos mismos valores como filas contiguas y conserva la multiplicación clásica. Para matrices de `1000 × 1000`, con GCC 14.2.0 y `-O3` en WSL sobre un Intel Core i5-1334U, la mediana bajó de 0.990 s a 0.391 s: una razón de mejora observada de 2.53× y una reducción de tiempo aproximada de 60.5 %. Todas las corridas registradas fueron más rápidas en la versión transpuesta y todas las verificaciones muestrales fueron satisfactorias.

La conclusión más sólida y acotada es que **esta transformación mejoró sustancialmente el rendimiento medido para este programa y esta configuración**, sin cambiar el resultado matemático esperado ni agregar memoria de matriz. La explicación probable es la lectura más contigua de los datos y el mejor aprovechamiento potencial de la jerarquía de memoria. El ensayo no midió los fallos de caché directamente y no basta para predecir el resultado en otros tamaños o plataformas.

## 12. Recomendaciones para fortalecer el experimento

1. Repetir con varios tamaños —por ejemplo `600`, `1000`, `1500` y tamaños mayores si hay memoria suficiente— para observar cómo escala la mejora.
2. Aumentar las repeticiones, por ejemplo a 10 o 20, y conservar todas las filas individuales.
3. Registrar alimentación, plan de energía, temperatura/carga aproximada y actividad relevante del equipo.
4. Si es posible, habilitar contadores de hardware y medir fallos de caché; interpretar con cuidado `perf` porque el script cuenta el proceso completo, no únicamente la región cronometrada.
5. Añadir una comparación directa de resultados `C` entre versiones para los mismos datos, además de las verificaciones muestrales internas.
6. Mantener iguales compilador, opciones, semilla, límite de valores, tamaños y condiciones al comparar; cambiar una variable experimental a la vez.

## 13. Comando reproducible usado

Desde Ubuntu/WSL, en la carpeta del laboratorio:

```bash
TAMANOS="1000" BANDERAS="-O3" \
  ./comparar_transpuesta.sh 5 42 matriz_secuencial.c matriz_secuencial_transpuesta.c
```

Los argumentos son cinco repeticiones, semilla 42 y los nombres de las fuentes base y transpuesta. El script guarda los CSV y `info_sistema.txt` en el directorio actual.
