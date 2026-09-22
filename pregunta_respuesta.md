# Pregunta y respuesta

## Pregunta

```c
#ifdef _WIN32
#include <windows.h>
#endif
```

¿Para que sirve?

## Respuesta

`_WIN32` es una macro que el compilador define automaticamente cuando el programa se compila en Windows.

- `#ifdef _WIN32` significa: "si estamos compilando en Windows".
- `#include <windows.h>` incluye las funciones y tipos de la API de Windows.
- `#endif` termina la condicion del preprocesador.

En este programa, `windows.h` se necesita en Windows para usar `LARGE_INTEGER`, `QueryPerformanceFrequency` y `QueryPerformanceCounter`, que permiten medir el tiempo de la multiplicacion con un reloj de alta resolucion.

Cuando el programa se compila en Linux o macOS, ese encabezado no se incluye y se utiliza la alternativa basada en `clock()` definida en la funcion `ahora()`.

## Pregunta

¿Que significa "Reserva dinamica de una matriz NxN como bloque contiguo"?

## Respuesta

Significa que el programa solicita memoria durante la ejecucion para almacenar una matriz cuadrada de `N` filas por `N` columnas. La expresion `NxN` indica que la matriz tiene `N` filas y `N` columnas.

En `crear_matriz`, primero se reserva un arreglo de punteros, uno por cada fila. Despues se reserva un unico bloque continuo de memoria con espacio para los `N*N` valores de la matriz. Cada puntero de fila se coloca en la posicion correspondiente dentro de ese bloque.

Esta organizacion permite acceder a los elementos de forma sencilla usando `m[i][j]`, como si fuera una matriz tradicional, pero utilizando memoria dinamica. Ademas, al estar todos los valores juntos en memoria, se aprovecha mejor la memoria cache y el acceso suele ser mas eficiente.

Cuando la matriz deja de utilizarse, `liberar_matriz` libera primero el bloque que contiene los valores y despues el arreglo de punteros. Asi se evita dejar memoria ocupada innecesariamente.

## Pregunta

¿El programa puede ejecutarse en Windows o necesita obligatoriamente Linux?

## Respuesta

El programa puede ejecutarse en Windows y no necesita obligatoriamente Linux. El codigo ya incluye una adaptacion para cada sistema operativo:

- En Windows, la macro `_WIN32` permite incluir `windows.h` y utilizar `QueryPerformanceCounter` para medir el tiempo con alta precision.
- En Linux y otros sistemas, no se incluye `windows.h` y la funcion `ahora()` utiliza `clock()` como alternativa.

Para ejecutarlo en Windows se necesita un compilador de C, por ejemplo GCC mediante MinGW o MSYS2, o un compilador compatible de Visual Studio. Con GCC, despues de compilar el archivo, se puede ejecutar desde la terminal con:

```text
gcc matmul.c -o matmul.exe
matmul.exe 600
```

En Linux se puede compilar y ejecutar de forma equivalente:

```text
gcc matmul.c -o matmul
./matmul 600
```

El numero `600` es el tamaño minimo permitido para la matriz. Tambien se pueden proporcionar los parametros opcionales definidos al inicio del programa.

## Pregunta

¿Qué es la reserva dinámica de memoria y cómo se usa?

## Respuesta

La reserva dinámica es una forma de pedir memoria en tiempo de ejecución, cuando el programa ya está corriendo. Esto sirve cuando no sabes de antemano cuántos datos vas a necesitar.

En C se usa con funciones como:

- `malloc()` para reservar memoria
- `calloc()` para reservar memoria e inicializarla en 0
- `realloc()` para cambiar el tamaño de la memoria ya reservada
- `free()` para liberar la memoria cuando ya no se usa

Ejemplo sencillo:

```c
#include <stdio.h>
#include <stdlib.h>

int main() {
    int *arr;
    int n = 5;

    arr = (int *)malloc(n * sizeof(int));

    if (arr == NULL) {
        printf("No se pudo reservar memoria\n");
        return 1;
    }

    for (int i = 0; i < n; i++) {
        arr[i] = i + 1;
    }

    for (int i = 0; i < n; i++) {
        printf("%d ", arr[i]);
    }

    free(arr);
    return 0;
}
```

¿Qué hace aquí?

- `malloc(n * sizeof(int))` pide memoria para 5 enteros.
- `arr` apunta al primer byte de esa memoria.
- Se puede usar como si fuera un arreglo normal.
- `free(arr)` libera la memoria para que el sistema la pueda reutilizar.

Se usa principalmente cuando el tamaño de la estructura depende del usuario o del cálculo del programa. Es muy útil para matrices, listas, cadenas y otras estructuras que no tienen tamaño fijo.

La parte más importante es recordar que cada `malloc()` o `calloc()` debe ir acompañado de un `free()` para evitar fugas de memoria.

## Pregunta

¿Como funciona `clock()` para medir el tiempo?

## Respuesta

`clock()` es una funcion de C que indica cuanto tiempo de procesador ha utilizado el programa desde que comenzo. Devuelve el tiempo en una unidad interna llamada ciclos de reloj.

Para convertir ese valor a segundos se divide entre `CLOCKS_PER_SEC`:

```c
(double)clock() / CLOCKS_PER_SEC
```

En este programa, la conversion se realiza dentro de la funcion `ahora()`:

```c
return (double)clock() / CLOCKS_PER_SEC;
```

La multiplicacion se mide tomando dos valores de tiempo:

```c
double t0 = ahora();
multiplicar(A, B, C, n);
double t1 = ahora();
double segundos = t1 - t0;
```

`t0` representa el tiempo antes de multiplicar y `t1` el tiempo despues. La diferencia indica cuanto tardo la funcion `multiplicar()`.

En Windows el programa usa `QueryPerformanceCounter()` porque ofrece una medicion de mayor resolucion. En otros sistemas utiliza `clock()` como alternativa.
