/*
 * matmul.c - Multiplicación de matrices cuadradas (C = A x B)
 *
 * Uso (todo por línea de comandos, sin entrada interactiva):
 *   ./matmul <tamaño> [limite] [verificar] [semilla] [hilos]
 *
 *   tamaño    : dimensión N de las matrices NxN (mínimo 600)      [obligatorio]
 *   limite    : valor máximo de las celdas (enteros 0..limite)    [opcional, def: 100]
 *   verificar : 1 = verificación de resultados, 0 = no            [opcional, def: 1]
 *   semilla   : semilla del generador aleatorio                   [opcional, def: tiempo]
 *   hilos     : cantidad de hilos para la multiplicacion          [opcional, def: nucleos disponibles]
 *
 * Características:
 *   - Matrices cuadradas con reserva dinámica de memoria (malloc/free)
 *   - Contenido aleatorio, enteros entre 0 y limite
 *   - No imprime matrices de entrada ni de salida
 *   - Multiplicación paralela usando pthreads (reparto de filas por hilo)
 *   - Verificación opcional: recomputa una muestra de celdas de C
 *     y las compara contra el resultado del algoritmo
 *
 * Compilación (Linux / WSL):
 *   gcc -O2 -pthread matmul.c -o matmul
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/resource.h>

#define MIN_SIZE 600

/* Reserva dinámica de una matriz NxN como bloque contiguo */
static long long **crear_matriz(int n)
{
    long long **m = (long long **)malloc((size_t)n * sizeof(long long *));
    if (m == NULL) return NULL;
    m[0] = (long long *)malloc((size_t)n * (size_t)n * sizeof(long long));
    if (m[0] == NULL) { free(m); return NULL; }
    for (int i = 1; i < n; i++)
        m[i] = m[0] + (size_t)i * n;
    return m;
}

static void liberar_matriz(long long **m)
{
    if (m != NULL) {
        free(m[0]);
        free(m);
    }
}

/* Llena la matriz con enteros aleatorios en [0, limite]. */
static void llenar_aleatorio(long long **m, int n, int limite)
{
    long long rango = (long long)RAND_MAX + 1;
    long long valores_posibles = (long long)limite + 1;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            m[i][j] = ((long long)rand() * rango + rand()) % valores_posibles;
}

/* Datos que recibe cada hilo para procesar un rango de filas */
typedef struct {
    long long **a;
    long long **b;
    long long **c;
    int n;
    int fila_inicio; /* inclusive */
    int fila_fin;    /* exclusivo */
} tarea_hilo_t;

/* Función ejecutada por cada hilo: multiplica su rango de filas */
static void *multiplicar_rango(void *arg)
{
    tarea_hilo_t *t = (tarea_hilo_t *)arg;
    for (int i = t->fila_inicio; i < t->fila_fin; i++) {
        for (int j = 0; j < t->n; j++) {
            long long suma = 0;
            for (int k = 0; k < t->n; k++)
                suma += (long long)t->a[i][k] * t->b[k][j];
            t->c[i][j] = suma;
        }
    }
    return NULL;
}

/* Multiplicación clásica O(n^3): C = A x B, repartida en 'num_hilos' hilos */
static void multiplicar(long long **a, long long **b, long long **c, int n, int num_hilos)
{
    if (num_hilos < 1) num_hilos = 1;
    if (num_hilos > n) num_hilos = n; /* no tiene sentido más hilos que filas */

    pthread_t *hilos = (pthread_t *)malloc((size_t)num_hilos * sizeof(pthread_t));
    tarea_hilo_t *tareas = (tarea_hilo_t *)malloc((size_t)num_hilos * sizeof(tarea_hilo_t));
    if (hilos == NULL || tareas == NULL) {
        fprintf(stderr, "Error: memoria insuficiente para %d hilos\n", num_hilos);
        free(hilos);
        free(tareas);
        exit(EXIT_FAILURE);
    }

    int filas_por_hilo = n / num_hilos;
    int resto = n % num_hilos;
    int fila_actual = 0;

    for (int t = 0; t < num_hilos; t++) {
        int filas_este_hilo = filas_por_hilo + (t < resto ? 1 : 0);
        tareas[t].a = a;
        tareas[t].b = b;
        tareas[t].c = c;
        tareas[t].n = n;
        tareas[t].fila_inicio = fila_actual;
        tareas[t].fila_fin = fila_actual + filas_este_hilo;
        fila_actual += filas_este_hilo;

        if (pthread_create(&hilos[t], NULL, multiplicar_rango, &tareas[t]) != 0) {
            fprintf(stderr, "Error: no se pudo crear el hilo %d de %d\n", t + 1, num_hilos);
            exit(EXIT_FAILURE);
        }
    }

    for (int t = 0; t < num_hilos; t++)
        pthread_join(hilos[t], NULL);

    free(hilos);
    free(tareas);
}

/*
 * Verificación: toma una muestra determinista de celdas de C
 * y recomputa cada producto punto fila x columna de forma independiente.
 * Devuelve el número de discrepancias encontradas (0 = correcto).
 */
static long verificar(long long **a, long long **b, long long **c, int n)
{
    long celdas = (long)n * n;
    long muestras = celdas < 1000 ? celdas : 1000; /* hasta 1000 celdas */
    long errores = 0;
    unsigned int estado = 12345u;

    for (long s = 0; s < muestras; s++) {
        /* generador propio para no alterar la secuencia de rand() */
        estado = estado * 1103515245u + 12345u;
        int i = (int)((estado >> 16) % (unsigned int)n);
        estado = estado * 1103515245u + 12345u;
        int j = (int)((estado >> 16) % (unsigned int)n);

        long long esperado = 0;
        for (int k = 0; k < n; k++)
            esperado += (long long)a[i][k] * b[k][j];

        if (esperado != c[i][j])
            errores++;
    }
    return errores;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <tamaño> [limite] [verificar] [semilla] [hilos]\n", argv[0]);
        fprintf(stderr, "  tamaño >= %d, limite >= 0, verificar = 0|1, hilos >= 1\n", MIN_SIZE);
        return EXIT_FAILURE;
    }

    int n        = atoi(argv[1]);
    int limite   = (argc > 2) ? atoi(argv[2]) : 100;
    int verif    = (argc > 3) ? atoi(argv[3]) : 1;
    unsigned int semilla = (argc > 4) ? (unsigned int)strtoul(argv[4], NULL, 10)
                                      : (unsigned int)time(NULL);

    long nucleos_disponibles = sysconf(_SC_NPROCESSORS_ONLN);
    if (nucleos_disponibles < 1) nucleos_disponibles = 1;
    int num_hilos = (argc > 5) ? atoi(argv[5]) : (int)nucleos_disponibles;

    if (n < MIN_SIZE) {
        fprintf(stderr, "Error: el tamaño mínimo es %d (recibido: %d)\n", MIN_SIZE, n);
        return EXIT_FAILURE;
    }
    if (limite < 0) {
        fprintf(stderr, "Error: el limite debe ser >= 0 (recibido: %d)\n", limite);
        return EXIT_FAILURE;
    }
    if (num_hilos < 1) {
        fprintf(stderr, "Error: la cantidad de hilos debe ser >= 1 (recibido: %d)\n", num_hilos);
        return EXIT_FAILURE;
    }

    srand(semilla);

    /* Reserva dinámica de memoria */
    long long **A = crear_matriz(n);
    long long **B = crear_matriz(n);
    long long **C = crear_matriz(n);
    if (A == NULL || B == NULL || C == NULL) {
        fprintf(stderr, "Error: memoria insuficiente para matrices de %dx%d\n", n, n);
        liberar_matriz(A); liberar_matriz(B); liberar_matriz(C);
        return EXIT_FAILURE;
    }

    /* Generación aleatoria de contenido (enteros entre 0 y limite) */
    llenar_aleatorio(A, n, limite);
    llenar_aleatorio(B, n, limite);

    /* Mide el tiempo total real transcurrido del proceso completo. */
    struct timespec reloj_inicio, reloj_fin;
    clock_gettime(CLOCK_MONOTONIC, &reloj_inicio);
    multiplicar(A, B, C, n, num_hilos);
    clock_gettime(CLOCK_MONOTONIC, &reloj_fin);

    double segundos_pared =
        (double)(reloj_fin.tv_sec - reloj_inicio.tv_sec) +
        (double)(reloj_fin.tv_nsec - reloj_inicio.tv_nsec) / 1e9;

    /* Solo se reportan métricas; nunca se imprimen las matrices */
    printf("Multiplicacion de matrices %dx%d completada.\n", n, n);
    printf("Limite de valores: %d | Semilla: %u | Hilos: %d\n", limite, semilla, num_hilos);
    printf("Tiempo total del proceso: %.3f segundos\n", segundos_pared);

    if (verif) {
        long errores = verificar(A, B, C, n);
        if (errores == 0)
            printf("Verificacion: OK (muestra de celdas recomputada sin discrepancias).\n");
        else {
            printf("Verificacion: FALLO (%ld discrepancias).\n", errores);
            liberar_matriz(A); liberar_matriz(B); liberar_matriz(C);
            return EXIT_FAILURE;
        }
    }

    liberar_matriz(A);
    liberar_matriz(B);
    liberar_matriz(C);
    return EXIT_SUCCESS;
}
