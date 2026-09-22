# Explicacion resumida de `matmul.c`

## Objetivo

El programa crea dos matrices cuadradas `A` y `B`, las llena con numeros aleatorios y calcula `C = A x B`. Tambien mide el tiempo de calculo y puede verificar una muestra del resultado. El tamano minimo es `600 x 600`.

## Funciones

- `crear_matriz(int n)`: reserva memoria dinamica para una matriz `n x n` y devuelve `NULL` si falla.
- `liberar_matriz(long long **m)`: libera la memoria reservada para una matriz.
- `llenar_aleatorio(long long **m, int n, int limite)`: llena la matriz con valores aleatorios entre `1` y `limite`.
- `multiplicar(long long **a, long long **b, long long **c, int n, int num_hilos)`: calcula `C = A x B` usando varios hilos POSIX (`pthreads`) y reparte las filas de `C` entre ellos. Su complejidad total sigue siendo `O(n^3)`.
- `multiplicar_rango(void *arg)`: funcion que ejecuta cada hilo. Recorre solo el rango de filas que le fue asignado y calcula cada celda mediante el producto punto entre una fila de `A` y una columna de `B`.
- `tarea_hilo_t`: estructura que contiene las matrices, el tamano y las filas inicial y final que debe procesar cada hilo.
- `ahora(void)`: obtiene el tiempo transcurrido en segundos usando `clock_gettime` y un reloj monotono.
- `ciclos_ahora(void)`: lee el contador TSC del procesador mediante `__rdtsc()` para conocer los ciclos de reloj transcurridos.
- `verificar(long long **a, long long **b, long long **c, int n)`: recalcula hasta 1000 celdas de `C` y las compara con el resultado obtenido.
- `main(int argc, char *argv[])`: coordina todo el programa: lee argumentos, valida datos, reserva memoria, genera matrices, multiplica, verifica y libera memoria.

## Flujo del programa

1. Lee los argumentos:

   ```text
   matmul <tamano> [limite] [verificar] [semilla] [hilos]
   ```

2. Valida el tamano y el limite.
3. Inicializa la semilla aleatoria.
4. Reserva `A`, `B` y `C`.
5. Llena `A` y `B` con valores aleatorios.
6. Lee el tiempo y el contador de ciclos iniciales, y ejecuta la multiplicacion.
7. Lee nuevamente el tiempo y el contador de ciclos, calcula las diferencias y muestra las metricas.
8. Verifica una muestra si esta activada la opcion.
9. Libera la memoria y termina.

## Multiplicacion con multihilos

La multiplicacion utiliza la biblioteca `pthreads`. El programa obtiene por defecto
la cantidad de nucleos disponibles, aunque tambien permite indicar manualmente el
numero de hilos mediante el quinto argumento.

La funcion `multiplicar` divide las `n` filas entre `num_hilos`. Si las filas no se
pueden repartir exactamente, los primeros hilos reciben una fila adicional. Para
cada hilo se crea una estructura `tarea_hilo_t` con las matrices y su rango de filas,
y luego `pthread_create` inicia la funcion `multiplicar_rango`.

Cada hilo calcula de forma independiente sus filas de `C` usando los tres ciclos de
la multiplicacion clasica. No hay conflicto entre hilos porque cada uno escribe en
filas diferentes de `C`, mientras que `A` y `B` solo se leen. Finalmente,
`pthread_join` hace que el programa espere a todos los hilos antes de medir el tiempo
final y mostrar el resultado. La cantidad de hilos nunca supera la cantidad de filas,
porque no seria util crear hilos sin trabajo.

## Conteo de ciclos de reloj

Ademas del tiempo en segundos, el programa mide los ciclos de reloj del procesador
que transcurren durante la multiplicacion. La funcion `ciclos_ahora` utiliza
`__rdtsc()`, una instruccion disponible en procesadores x86 y x86_64, para leer el
contador TSC (Time Stamp Counter).

Antes de llamar a `multiplicar`, `main` guarda el valor inicial `ciclos0`. Cuando
todos los hilos terminan, guarda el valor final `ciclos1` y calcula:

```text
ciclos_totales = ciclos1 - ciclos0
```

El resultado se muestra con el mensaje `Ciclos de reloj (TSC)`. Este valor representa
los ciclos transcurridos como tiempo de pared durante toda la multiplicacion,
incluyendo el trabajo paralelo y la coordinacion de los hilos. No es la suma de los
ciclos consumidos individualmente por cada hilo. Por eso se reporta junto con el
tiempo en segundos: ambas medidas permiten comparar el costo de la operacion y el
efecto de utilizar diferentes cantidades de hilos.

## Ejemplo

```text
matmul 600 100 1 12345 4
```

Crea matrices de `600 x 600`, usa valores entre `1` y `100`, activa la verificacion,
utiliza `12345` como semilla y realiza la multiplicacion con 4 hilos.
