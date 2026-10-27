# Informe: optimización de la multiplicación secuencial de matrices

## Objetivo

Comparar el tiempo de ejecución del programa de multiplicación de matrices al compilarlo con distintas opciones de optimización de GCC. La versión del programa sigue siendo **secuencial** en todas las pruebas: las opciones del compilador no agregan hilos ni procesos explícitos.

## Qué calcula el programa

El programa genera dos matrices cuadradas `A` y `B` de dimensión `N × N` y calcula `C = A × B`. Cada elemento se obtiene mediante un producto punto:

```text
C[i][j] = suma para k = 0..N-1 de A[i][k] * B[k][j]
```

El algoritmo tiene tres ciclos anidados y complejidad `O(N³)`. Para `N = 1000` calcula mil millones de productos y aproximadamente mil millones de acumulaciones (del orden de **2 mil millones de operaciones aritméticas**, sin contar accesos a memoria ni el resto de instrucciones). El programa reserva tres matrices de `long long`; suponiendo 8 bytes por elemento, sus datos ocupan alrededor de 24 MB en total.

## Configuración del experimento

- **Tamaño:** `1000 × 1000`.
- **Repeticiones medidas:** 5 por variante.
- **Semilla:** 42.
- **Límite de los valores aleatorios:** 100.
- **Compilador:** GCC 14.2.0, en Debian bajo WSL.
- **Procesador reportado:** Intel Core i5-1334U de 13.ª generación.
- Se hizo una corrida de calentamiento por variante y se descartó.
- El tiempo de pared reportado por el programa mide la llamada a la multiplicación; no incluye la reserva ni el llenado de las matrices. La verificación se ejecuta después de medir el tiempo.
- La verificación recomputa una muestra de hasta 1.000 celdas de `C`; no compara la matriz completa.

## Resultados

El *speedup* se calcula respecto de `-O0`, usando las medianas de tiempo de pared:

```text
speedup = mediana(-O0) / mediana(variante)
reducción de tiempo (%) = (1 - mediana(variante) / mediana(-O0)) × 100
```

| Variante | Banderas de compilación | Mediana (s) | Mínimo (s) | Speedup vs. `-O0` | Menos tiempo que `-O0` |
|---|---|---:|---:|---:|---:|
| O0 | `-O0` | 2.587 | 2.407 | 1.00× | 0.0 % |
| O1 | `-O1` | 1.067 | 1.006 | 2.42× | 58.7 % |
| O2 | `-O2` | 1.274 | 1.196 | 2.03× | 50.8 % |
| O3 | `-O3` | 1.077 | 0.977 | 2.40× | 58.4 % |
| O3 + desenrollado | `-O3 -funroll-loops` | 1.142 | 1.100 | 2.27× | 55.9 % |
| O3 + arquitectura nativa + desenrollado | `-O3 -march=native -funroll-loops` | 1.228 | 1.030 | 2.11× | 52.5 % |

Por ejemplo, para `-O1`:

```text
speedup = 2.587 / 1.067 ≈ 2.42
reducción = (1 - 1.067 / 2.587) × 100 ≈ 58.7 %
```

Esto significa que, según las medianas observadas, el tiempo de `-O1` fue aproximadamente 1/2.42 del tiempo de la línea base, es decir, redujo la duración alrededor de un 58.7 %. No significa que el procesador tenga 2.42 veces más núcleos ni que la ejecución se haya paralelizado.

## Qué significan las opciones probadas

- **`-O0`:** desactiva la mayoría de las optimizaciones. Es la referencia de comparación, no necesariamente una configuración recomendable para medir el rendimiento final.
- **`-O1`:** activa optimizaciones que suelen reducir tiempo y tamaño del código sin aplicar todas las transformaciones más agresivas.
- **`-O2`:** activa un conjunto más amplio de optimizaciones para rendimiento. En esta medición fue más lento que `-O1` y `-O3`; el nombre de la opción no garantiza que siempre sea más rápida para cada programa.
- **`-O3`:** añade optimizaciones más agresivas que `-O2`. Aquí su mediana fue muy cercana a la de `-O1`.
- **`-funroll-loops`:** permite que GCC desenrolle ciertos ciclos, expandiendo varias iteraciones en el código generado. Puede reducir el control del ciclo, pero también aumentar el tamaño del código y no necesariamente mejorar el rendimiento.
- **`-march=native`:** permite generar instrucciones adaptadas al procesador donde se compila. El binario resultante puede no funcionar en procesadores más antiguos o diferentes.

Estas banderas permiten al compilador transformar el código, pero los resultados no identifican por sí solos qué transformaciones se aplicaron ni cuál causó cada diferencia.

## Interpretación y conclusiones

1. **Las optimizaciones mejoraron claramente el tiempo frente a `-O0`.** Las variantes optimizadas tardaron entre aproximadamente 1.07 y 1.27 segundos, mientras que `-O0` tardó 2.587 segundos de mediana.
2. **La mejor mediana observada fue `-O1` (1.067 s), seguida de cerca por `-O3` (1.077 s).** La diferencia entre ambas es de 0.010 s, menos del 1 % del tiempo de `-O1`; con solo cinco repeticiones no hay evidencia suficiente para afirmar que una sea consistentemente superior.
3. **En esta prueba, `-O2` no fue la opción más rápida.** También el desenrollado y `-march=native` combinado con desenrollado tardaron más que `-O1` y `-O3`. Esto no demuestra que esas banderas siempre perjudiquen: el resultado depende del programa, del compilador, del procesador y de las condiciones de ejecución.
4. **La verificación fue `OK` en las seis variantes.** Esto confirma que la muestra comprobada coincidió con el resultado calculado en cada ejecución, pero no es una prueba exhaustiva de cada celda.
5. **El experimento mide el efecto de compilar este mismo algoritmo con distintas banderas**, no el beneficio del paralelismo ni el de cambiar el algoritmo. El patrón de acceso del ciclo interno lee `B[k][j]` por columnas, aunque los datos se almacenan contiguos por filas; ese acceso puede afectar la caché, pero esta prueba no mide ni aísla ese efecto.

## Variabilidad de las mediciones

La brecha entre la mediana y el mínimo indica qué tan estables fueron las cinco corridas de cada variante. Se calcula como `(mediana - mínimo) / mediana`.

| Variante | Mediana (s) | Mínimo (s) | Brecha |
|---|---:|---:|---:|
| `-O0` | 2.587 | 2.407 | 7 % |
| `-O1` | 1.067 | 1.006 | 6 % |
| `-O2` | 1.274 | 1.196 | 6 % |
| `-O3` | 1.077 | 0.977 | 9 % |
| `-O3 -funroll-loops` | 1.142 | 1.100 | 4 % |
| `-O3 -march=native -funroll-loops` | 1.228 | 1.030 | 16 % |

Como criterio de este informe (no un estándar), una diferencia entre variantes menor que la brecha de las mediciones involucradas no se considera concluyente.

- La diferencia entre `-O1` y `-O3` (menos del 1 %) es mucho menor que sus brechas (6 % y 9 %), por lo que no se puede declarar una ganadora.
- `-O2` quedó aproximadamente un 19 % por encima de `-O1` en mediana, una diferencia mayor que las brechas observadas. Aun así, al no conservar los tiempos individuales, no se puede confirmar si las corridas de ambas variantes se solapan.
- `-O3 -march=native -funroll-loops` fue la medición menos estable (16 %): su mediana es peor que la de `-O3`, pero su mejor corrida (1.030 s) se acerca a ella. Con estos datos no se puede concluir que sea peor ni mejor.
- El Intel Core i5-1334U combina núcleos de rendimiento y de eficiencia, y esta prueba no fijó el programa a un núcleo específico. Es una hipótesis, no comprobada, que parte de la variabilidad se deba a ello.

## Condiciones de ejecución

- Alimentación del portátil: [completar: conectado a la corriente / con batería].
- Plan de energía de Windows: [completar: por ejemplo, alto rendimiento / equilibrado].
- Otros programas abiertos durante la prueba: [completar].
- Entorno: Debian bajo WSL, ejecutado desde `/mnt/c/...` (el sistema de archivos de Windows). El tiempo reportado solo cubre la multiplicación, por lo que esto no debería afectarlo.

## Cómo reproducir el experimento

Desde la carpeta que contiene el programa y el script:

```bash
chmod +x comparar_flags.sh
./comparar_flags.sh 1000 5 42 matriz_secuencial.c
```

Los parámetros son tamaño de matriz, repeticiones, semilla y archivo fuente. El script compila cada variante, descarta una corrida de calentamiento, mide cinco corridas y guarda el resumen en `resultados_flags.csv`.

## Limitaciones

- Se probó un único tamaño de matriz (N = 1000); no se sabe si el patrón se mantiene con otros tamaños.
- Solo hubo cinco repeticiones por variante y no se guardaron los tiempos individuales, solo la mediana y el mínimo.
- Todas las mediciones se hicieron en un solo equipo, con un procesador híbrido y sin fijar el núcleo de ejecución.
- La verificación recomputa una muestra de hasta 1.000 celdas; no compara la matriz completa.
- No se inspeccionó qué transformaciones aplicó el compilador en cada variante, por lo que las diferencias no se pueden atribuir a una optimización concreta.
- El efecto del acceso a `B[k][j]` por columnas no se midió.
- Los resultados de `-march=native` no son portables: el binario depende del procesador donde se compila.

## Decisión sobre las banderas

Se adopta **`-O3`** como configuración base de compilación para todas las versiones del programa (secuencial, con hilos y con procesos):

```bash
gcc -O3 matriz_secuencial.c -o matriz_secuencial
```

Criterios de la decisión:

1. `-O1` y `-O3` son estadísticamente indistinguibles en esta prueba (menos del 1 % de diferencia en la mediana), por lo que la elección se basa en criterios adicionales y no en una superioridad medida.
2. `-O3` obtuvo el mejor tiempo mínimo observado (0.977 s).
3. `-O3` es el nivel estándar de optimización más alto de GCC y no depende del procesador, a diferencia de `-march=native`.
4. Se descartan `-funroll-loops` y `-march=native` porque no mostraron una mejora fiable y la segunda reduce la portabilidad.
5. Compilar todas las versiones con las mismas banderas permite que la comparación entre secuencial, hilos y procesos refleje el efecto del paralelismo y no el de las banderas. Las versiones con hilos y procesos se compilarán con `-O3` (añadiendo `-pthread` donde corresponda) y se verificará que esta elección no las perjudique.

Para calcular el speedup y la eficiencia de las versiones paralelas, el tiempo base de referencia será el de la versión secuencial compilada con `-O3`.

## Siguientes pasos

1. Modificar el script para guardar los tiempos de cada corrida individual.
2. Repetir el experimento con más tamaños (por ejemplo 600, 1000 y 1500) y con 10 repeticiones.
3. Compilar con `-O3` las versiones con hilos y con procesos y repetir las mediciones.
4. Revisar con `-fopt-info-vec-all`, `-fopt-info-loop-all` y `gcc -Q --help=optimizers` qué transformaciones aplica GCC 14.2 en cada nivel.
5. Medir el efecto del orden de los bucles y del acceso a `B` por columnas.
6. Si la variabilidad persiste, fijar la ejecución a un núcleo (`taskset`) tras revisar la topología con `lscpu`, y comparar.

### Conclusión general

Para esta computadora, esta versión de GCC y matrices de tamaño 1000, `-O1` y `-O3` dieron el mejor rendimiento observado y redujeron el tiempo mediano entre un 58 % y un 59 % frente a `-O0`. Con los datos disponibles, ambas deben considerarse prácticamente empatadas: `-O1` tiene la mejor mediana (1.067 s) y `-O3` el mejor tiempo mínimo (0.977 s). Las opciones `-O2`, `-funroll-loops` y `-march=native` no ofrecieron una mejora sobre ellas en esta prueba. Como los tiempos de cada corrida individual no se guardaron, no es posible determinar si los rangos de las variantes se solapan; por eso las diferencias pequeñas no se consideran concluyentes.