/*
 * matmul_procesos.c - Multiplicación de matrices cuadradas (C = A x B)
 *                     Versión paralela con PROCESOS (fork) en lugar de hilos.
 *
 * Uso (todo por línea de comandos, sin entrada interactiva):
 *   ./matmul_procesos <tamaño> [limite] [verificar] [semilla] [procesos]
 *
 *   tamaño    : dimensión N de las matrices NxN (mínimo 600)      [obligatorio]
 *   limite    : valor máximo de las celdas (enteros 0..limite)    [opcional, def: 100]
 *   verificar : 1 = verificación de resultados, 0 = no            [opcional, def: 1]
 *   semilla   : semilla del generador aleatorio                   [opcional, def: tiempo]
 *   procesos  : cantidad de procesos hijos para la multiplicacion [opcional, def: nucleos disponibles]
 *
 * Características:
 *   - Matrices cuadradas con reserva dinámica de memoria
 *   - Contenido aleatorio, enteros entre 0 y limite
 *   - No imprime matrices de entrada ni de salida
 *   - Multiplicación paralela usando fork() (reparto de filas por proceso)
 *   - A y B se reservan con malloc: los hijos las heredan y, como solo las
 *     LEEN, Linux las comparte físicamente (copy-on-write, sin copiarlas).
 *   - C se reserva con mmap(MAP_SHARED | MAP_ANONYMOUS) ANTES del fork:
 *     es la memoria compartida explícita donde todos los hijos escriben
 *     sus filas y donde el padre lee el resultado final.
 *   - Cada hijo escribe filas distintas de C, por lo que no hay sección
 *     crítica y no se necesitan semáforos. La sincronización final se hace
 *     con waitpid() (equivalente al pthread_join de la versión con hilos).
 *   - Verificación opcional: recomputa una muestra de celdas de C
 *     y las compara contra el resultado del algoritmo
 *
 * Compilación (Linux / WSL):
 *   gcc -O2 matmul_procesos.c -o matmul_procesos
 */

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/resource.h>

#define MIN_SIZE 600

/* Reserva dinámica de una matriz NxN como bloque contiguo (memoria PRIVADA) */
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

/*
 * Reserva una matriz NxN cuyo bloque de datos vive en MEMORIA COMPARTIDA
 * (mmap anónimo compartido). El arreglo de punteros a filas es un malloc
 * normal: cada proceso hijo hereda su propia copia, y como los punteros
 * apuntan a la misma dirección virtual del bloque compartido, m[i][j]
 * funciona igual en el padre y en los hijos.
 */
static long long **crear_matriz_compartida(int n)
{
    long long **m = (long long **)malloc((size_t)n * sizeof(long long *));
    if (m == NULL)
        return NULL;
    void *bloque = mmap(NULL, (size_t)n * (size_t)n * sizeof(long long),
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (bloque == MAP_FAILED)
    {
        free(m);
        return NULL;
    }
    m[0] = (long long *)bloque;
    for (int i = 1; i < n; i++)
        m[i] = m[0] + (size_t)i * n;
    return m;
}

static void liberar_matriz_compartida(long long **m, int n)
{
    if (m != NULL)
    {
        munmap(m[0], (size_t)n * (size_t)n * sizeof(long long));
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

/* Trabajo de cada proceso hijo: multiplica su rango de filas [inicio, fin) */
static void multiplicar_rango(long long **a, long long **b, long long **c,
                              int n, int fila_inicio, int fila_fin)
{
    for (int i = fila_inicio; i < fila_fin; i++)
    {
        for (int j = 0; j < n; j++)
        {
            long long suma = 0;
            for (int k = 0; k < n; k++)
                suma += a[i][k] * b[k][j];
            c[i][j] = suma;
        }
    }
}

/*
 * Multiplicación clásica O(n^3): C = A x B, repartida en 'num_procesos'
 * procesos hijos creados con fork(). C debe estar en memoria compartida.
 * Devuelve 0 si todos los hijos terminaron bien, distinto de 0 si alguno falló.
 */
static int multiplicar(long long **a, long long **b, long long **c, int n, int num_procesos)
{
    if (num_procesos < 1)
        num_procesos = 1;
    if (num_procesos > n)
        num_procesos = n; /* no tiene sentido más procesos que filas */

    pid_t *pids = (pid_t *)malloc((size_t)num_procesos * sizeof(pid_t));
    if (pids == NULL)
    {
        fprintf(stderr, "Error: memoria insuficiente para %d procesos\n", num_procesos);
        exit(EXIT_FAILURE);
    }

    int filas_por_proceso = n / num_procesos;
    int resto = n % num_procesos;
    int fila_actual = 0;
    int creados = 0;
    int fallo_creacion = 0;

    /* Evita que el buffer de stdout se duplique en los hijos */
    fflush(stdout);
    fflush(stderr);

    for (int t = 0; t < num_procesos; t++)
    {
        int filas_este_proceso = filas_por_proceso + (t < resto ? 1 : 0);
        int inicio = fila_actual;
        int fin = fila_actual + filas_este_proceso;
        fila_actual = fin;

        pid_t pid = fork();
        if (pid < 0)
        {
            fprintf(stderr, "Error: no se pudo crear el proceso %d de %d\n", t + 1, num_procesos);
            fallo_creacion = 1;
            break;
        }
        if (pid == 0)
        {
            /* ---- Código del HIJO ---- */
            multiplicar_rango(a, b, c, n, inicio, fin);
            _exit(EXIT_SUCCESS); /* el hijo NO debe seguir ejecutando el código del padre */
        }
        /* ---- Código del PADRE ---- */
        pids[creados++] = pid;
    }

    /* El padre espera a todos los hijos creados (equivale al pthread_join) */
    int fallos = fallo_creacion;
    for (int t = 0; t < creados; t++)
    {
        int estado = 0;
        if (waitpid(pids[t], &estado, 0) < 0)
        {
            perror("Error en waitpid");
            fallos++;
        }
        else if (!WIFEXITED(estado) || WEXITSTATUS(estado) != 0)
        {
            fprintf(stderr, "Error: el proceso hijo %d (pid %d) termino de forma anormal\n",
                    t + 1, (int)pids[t]);
            fallos++;
        }
    }

    free(pids);
    return fallos;
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

    for (long s = 0; s < muestras; s++)
    {
        /* generador propio para no alterar la secuencia de rand() */
        estado = estado * 1103515245u + 12345u;
        int i = (int)((estado >> 16) % (unsigned int)n);
        estado = estado * 1103515245u + 12345u;
        int j = (int)((estado >> 16) % (unsigned int)n);

        long long esperado = 0;
        for (int k = 0; k < n; k++)
            esperado += a[i][k] * b[k][j];

        if (esperado != c[i][j])
            errores++;
    }
    return errores;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Uso: %s <tamaño> [limite] [verificar] [semilla] [procesos]\n", argv[0]);
        fprintf(stderr, "  tamaño >= %d, limite >= 0, verificar = 0|1, procesos >= 1\n", MIN_SIZE);
        return EXIT_FAILURE;
    }

    int n = atoi(argv[1]);
    int limite = (argc > 2) ? atoi(argv[2]) : 100;
    int verif = (argc > 3) ? atoi(argv[3]) : 1;
    unsigned int semilla = (argc > 4) ? (unsigned int)strtoul(argv[4], NULL, 10)
                                      : (unsigned int)time(NULL);

    long nucleos_disponibles = sysconf(_SC_NPROCESSORS_ONLN);
    if (nucleos_disponibles < 1)
        nucleos_disponibles = 1;
    int num_procesos = (argc > 5) ? atoi(argv[5]) : (int)nucleos_disponibles;

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
    if (num_procesos < 1)
    {
        fprintf(stderr, "Error: la cantidad de procesos debe ser >= 1 (recibido: %d)\n", num_procesos);
        return EXIT_FAILURE;
    }

    srand(semilla);

    /* Reserva dinámica de memoria:
     *   A y B: privadas (solo lectura durante la multiplicación)
     *   C    : compartida entre procesos (mmap) para que el padre vea lo que escriben los hijos */
    long long **A = crear_matriz(n);
    long long **B = crear_matriz(n);
    long long **C = crear_matriz_compartida(n);
    if (A == NULL || B == NULL || C == NULL)
    {
        fprintf(stderr, "Error: memoria insuficiente para matrices de %dx%d\n", n, n);
        liberar_matriz(A);
        liberar_matriz(B);
        liberar_matriz_compartida(C, n);
        return EXIT_FAILURE;
    }

    /* Generación aleatoria de contenido (enteros entre 0 y limite) */
    llenar_aleatorio(A, n, limite);
    llenar_aleatorio(B, n, limite);

    /* Mide el tiempo total real transcurrido del proceso completo. */
    struct timespec reloj_inicio, reloj_fin;
    clock_gettime(CLOCK_MONOTONIC, &reloj_inicio);
    int fallos = multiplicar(A, B, C, n, num_procesos);
    clock_gettime(CLOCK_MONOTONIC, &reloj_fin);
    if (fallos != 0)
    {
        fprintf(stderr, "Error: fallaron %d proceso(s) durante la multiplicacion\n", fallos);
        liberar_matriz(A);
        liberar_matriz(B);
        liberar_matriz_compartida(C, n);
        return EXIT_FAILURE;
    }
    double segundos_pared =
        (double)(reloj_fin.tv_sec - reloj_inicio.tv_sec) +
        (double)(reloj_fin.tv_nsec - reloj_inicio.tv_nsec) / 1e9;

    /* Solo se reportan métricas; nunca se imprimen las matrices */
    printf("Multiplicacion de matrices %dx%d completada.\n", n, n);
    printf("Limite de valores: %d | Semilla: %u | Procesos: %d\n", limite, semilla, num_procesos);
    printf("Tiempo total del proceso: %.3f segundos\n", segundos_pared);

    if (verif)
    {
        long errores = verificar(A, B, C, n);
        if (errores == 0)
            printf("Verificacion: OK (muestra de celdas recomputada sin discrepancias).\n");
        else
        {
            printf("Verificacion: FALLO (%ld discrepancias).\n", errores);
            liberar_matriz(A);
            liberar_matriz(B);
            liberar_matriz_compartida(C, n);
            return EXIT_FAILURE;
        }
    }

    liberar_matriz(A);
    liberar_matriz(B);
    liberar_matriz_compartida(C, n);
    return EXIT_SUCCESS;
}