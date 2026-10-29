/*
 * matmul_openmp_transpuesta.c - Multiplicación de matrices cuadradas (C = A x B)
 *                       Versión PARALELA con OpenMP y B almacenada TRANSPUESTA.
 *
 * Uso (todo por línea de comandos, sin entrada interactiva):
 *   ./matmul_openmp_transpuesta <tamaño> [limite] [verificar] [semilla] [hilos]
 *
 *   tamaño    : dimensión N de las matrices NxN (mínimo 600)      [obligatorio]
 *   limite    : valor máximo de las celdas (enteros 0..limite)    [opcional, def: 100]
 *   verificar : 1 = verificación de resultados, 0 = no            [opcional, def: 1]
 *   semilla   : semilla del generador aleatorio                   [opcional, def: tiempo]
 *   hilos     : cantidad de hilos OpenMP a usar                   [opcional, def: los
 *               que OpenMP use por defecto (OMP_NUM_THREADS o nº de núcleos)]
 *
 * Paralelización:
 *   - El usuario decide CUÁNTOS hilos se usan (argumento "hilos").
 *   - OpenMP decide CÓMO se reparte el trabajo: se usa "#pragma omp parallel for"
 *     sobre el bucle externo (filas de C). Cada hilo calcula filas distintas de C,
 *     por lo que no hay condiciones de carrera: A y BT solo se leen, y cada celda
 *     C[i][j] la escribe un único hilo.
 *   - La generación aleatoria sigue siendo secuencial a propósito, para que con
 *     la misma semilla las matrices (y por tanto C) sean idénticas a la versión
 *     secuencial, sin importar el número de hilos.
 *   - Se mantiene la optimización de B transpuesta (fila x fila).
 *
 * Compilación (Linux / WSL): hay que agregar -fopenmp
 *   gcc -O3 -fopenmp matmul_openmp_transpuesta.c -o matmul_openmp_transpuesta
 *
 * Ejemplos:
 *   ./matmul_openmp_transpuesta 1000 100 1 42 4     # 4 hilos
 *   ./matmul_openmp_transpuesta 1000 100 1 42 1     # 1 hilo (comparación)
 *   OMP_NUM_THREADS=8 ./matmul_openmp_transpuesta 1000   # hilos por variable de entorno
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <omp.h>

#define MIN_SIZE 600

/* Reserva dinámica de una matriz NxN como bloque contiguo */
static long long **crear_matriz(int n)
{
    long long **m = (long long **)malloc((size_t)n * sizeof(long long *));
    if (m == NULL)
        return NULL;
    m[0] = (long long *)malloc((size_t)n * (size_t)n * sizeof(long long));
    if (m[0] == NULL)
    {
        free(m);
        return NULL;
    }
    for (int i = 1; i < n; i++)
        m[i] = m[0] + (size_t)i * n;
    return m;
}

static void liberar_matriz(long long **m)
{
    if (m != NULL)
    {
        free(m[0]);
        free(m);
    }
}

/* Llena la matriz con enteros aleatorios en [0, limite]. (Secuencial) */
static void llenar_aleatorio(long long **m, int n, int limite)
{
    long long rango = (long long)RAND_MAX + 1;
    long long valores_posibles = (long long)limite + 1;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            m[i][j] = ((long long)rand() * rango + rand()) % valores_posibles;
}

/*
 * Genera la matriz B directamente TRANSPUESTA: el valor lógico B[i][j]
 * se guarda en bt[j][i]. Se mantiene secuencial para consumir rand() en el
 * mismo orden que la versión secuencial (rand() no es seguro entre hilos
 * y paralelizarlo cambiaría los valores generados).
 */
static void llenar_aleatorio_transpuesta(long long **bt, int n, int limite)
{
    long long rango = (long long)RAND_MAX + 1;
    long long valores_posibles = (long long)limite + 1;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            bt[j][i] = ((long long)rand() * rango + rand()) % valores_posibles;
}

/*
 * Multiplicación clásica O(n^3): C = A x B, paralelizada con OpenMP.
 * Recibe BT (B transpuesta): el producto punto recorre la fila i de A y la
 * fila j de BT, ambas de forma contigua en memoria (fila x fila).
 *
 * - num_threads(hilos): cantidad de hilos elegida por el usuario.
 * - parallel for: OpenMP reparte las iteraciones de i entre los hilos.
 * - schedule(static): todas las filas cuestan lo mismo, así que un reparto
 *   estático en bloques contiguos es el de menor sobrecarga.
 * - j, k y suma se declaran dentro del bucle, así que son privadas de cada hilo.
 */
static void multiplicar(long long **a, long long **bt, long long **c, int n, int hilos)
{
#pragma omp parallel for num_threads(hilos) schedule(static)
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < n; j++)
        {
            long long suma = 0;
            for (int k = 0; k < n; k++)
                suma += a[i][k] * bt[j][k];
            c[i][j] = suma;
        }
    }
}

/*
 * Verificación: toma una muestra determinista de celdas de C
 * y recomputa cada producto punto de forma independiente (secuencial).
 * Devuelve el número de discrepancias encontradas (0 = correcto).
 */
static long verificar(long long **a, long long **bt, long long **c, int n)
{
    long celdas = (long)n * n;
    long muestras = celdas < 1000 ? celdas : 1000; /* hasta 1000 celdas */
    long errores = 0;
    unsigned int estado = 12345u;

    for (long s = 0; s < muestras; s++)
    {
        /* generador propio para no alterar la secuencia de rand() */
        estado = estado * 1103515245u + 12345u;
        int i = (int)((estado >> 16) % (unsigned int)n);
        estado = estado * 1103515245u + 12345u;
        int j = (int)((estado >> 16) % (unsigned int)n);

        long long esperado = 0;
        for (int k = 0; k < n; k++)
            esperado += a[i][k] * bt[j][k];

        if (esperado != c[i][j])
            errores++;
    }
    return errores;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Uso: %s <tamaño> [limite] [verificar] [semilla] [hilos]\n", argv[0]);
        fprintf(stderr, "  tamaño >= %d, limite >= 0, verificar = 0|1, hilos >= 1\n", MIN_SIZE);
        return EXIT_FAILURE;
    }

    int n = atoi(argv[1]);
    int limite = (argc > 2) ? atoi(argv[2]) : 100;
    int verif = (argc > 3) ? atoi(argv[3]) : 1;
    unsigned int semilla = (argc > 4) ? (unsigned int)strtoul(argv[4], NULL, 10)
                                      : (unsigned int)time(NULL);
    /* Si no se indica, se usa lo que OpenMP tenga por defecto
     * (variable OMP_NUM_THREADS o número de núcleos disponibles). */
    int hilos = (argc > 5) ? atoi(argv[5]) : omp_get_max_threads();

    if (n < MIN_SIZE)
    {
        fprintf(stderr, "Error: el tamaño mínimo es %d (recibido: %d)\n", MIN_SIZE, n);
        return EXIT_FAILURE;
    }
    if (limite < 0)
    {
        fprintf(stderr, "Error: el limite debe ser >= 0 (recibido: %d)\n", limite);
        return EXIT_FAILURE;
    }
    if (hilos < 1)
    {
        fprintf(stderr, "Error: la cantidad de hilos debe ser >= 1 (recibido: %d)\n", hilos);
        return EXIT_FAILURE;
    }

    srand(semilla);

    /* Reserva dinámica de memoria (BT en lugar de B: mismo tamaño) */
    long long **A = crear_matriz(n);
    long long **BT = crear_matriz(n);
    long long **C = crear_matriz(n);
    if (A == NULL || BT == NULL || C == NULL)
    {
        fprintf(stderr, "Error: memoria insuficiente para matrices de %dx%d\n", n, n);
        liberar_matriz(A);
        liberar_matriz(BT);
        liberar_matriz(C);
        return EXIT_FAILURE;
    }

    /* Generación aleatoria de contenido (enteros entre 0 y limite).
     * Se genera A y luego B, en el mismo orden que la versión base. */
    llenar_aleatorio(A, n, limite);
    llenar_aleatorio_transpuesta(BT, n, limite);

    /* Mide solo el tiempo de pared de la multiplicación (omp_get_wtime
     * es un reloj de pared portable proporcionado por OpenMP). */
    double inicio = omp_get_wtime();
    multiplicar(A, BT, C, n, hilos);
    double fin = omp_get_wtime();
    double segundos_pared = fin - inicio;

    /* Solo se reportan métricas; nunca se imprimen las matrices */
    printf("Multiplicacion de matrices %dx%d completada.\n", n, n);
    printf("Limite de valores: %d | Semilla: %u | B transpuesta (fila x fila)\n", limite, semilla);
    printf("Hilos OpenMP: %d\n", hilos);
    printf("Tiempo de pared: %.3f segundos\n", segundos_pared);

    if (verif)
    {
        long errores = verificar(A, BT, C, n);
        if (errores == 0)
            printf("Verificacion: OK (muestra de celdas recomputada sin discrepancias).\n");
        else
        {
            printf("Verificacion: FALLO (%ld discrepancias).\n", errores);
            liberar_matriz(A);
            liberar_matriz(BT);
            liberar_matriz(C);
            return EXIT_FAILURE;
        }
    }

    liberar_matriz(A);
    liberar_matriz(BT);
    liberar_matriz(C);
    return EXIT_SUCCESS;
}