/*
 * matmul_secuencial_transpuesta.c - Multiplicación de matrices cuadradas (C = A x B)
 *                       Versión SECUENCIAL con B almacenada TRANSPUESTA.
 *
 * Uso (todo por línea de comandos, sin entrada interactiva):
 *   ./matmul_secuencial_transpuesta <tamaño> [limite] [verificar] [semilla]
 *
 *   tamaño    : dimensión N de las matrices NxN (mínimo 600)      [obligatorio]
 *   limite    : valor máximo de las celdas (enteros 0..limite)    [opcional, def: 100]
 *   verificar : 1 = verificación de resultados, 0 = no            [opcional, def: 1]
 *   semilla   : semilla del generador aleatorio                   [opcional, def: tiempo]
 *
 * Idea de la optimización (localidad espacial / línea de caché):
 *   - La memoria se transfiere entre RAM y caché en bloques llamados líneas
 *     de caché (típicamente 64 bytes = 8 valores long long). Al leer un
 *     elemento, el procesador trae la línea completa.
 *   - Las matrices se guardan por filas (row-major). Recorrer una FILA usa
 *     las 8 posiciones de cada línea traída; recorrer una COLUMNA usa solo
 *     1 de las 8 y salta N*8 bytes entre accesos, por lo que cada acceso
 *     toca una línea distinta y puede expulsar líneas que aún se necesitan.
 *   - En C[i][j] = suma_k A[i][k] * B[k][j], A se recorre por filas (bien),
 *     pero B se recorre por COLUMNAS (mal).
 *   - Solución: al GENERAR B se guarda directamente transpuesta (BT), de modo
 *     que BT[j][k] == B[k][j]. Así el producto punto pasa a ser
 *     C[i][j] = suma_k A[i][k] * BT[j][k]: fila x fila, ambos secuenciales.
 *   - La matriz lógica B es la misma que en matmul_secuencial.c para una
 *     misma semilla (se consumen los números aleatorios en el mismo orden),
 *     por lo que C resulta idéntica; solo cambia cómo se guarda B en memoria.
 *
 * Características:
 *   - Matrices cuadradas con reserva dinámica de memoria (malloc/free)
 *   - Contenido aleatorio, enteros entre 0 y limite
 *   - No imprime matrices de entrada ni de salida
 *   - Multiplicación secuencial: sin hilos ni procesos
 *   - No usa memoria extra: BT ocupa el mismo espacio que ocuparía B
 *   - Mide tiempo de pared, de usuario y de sistema (solo la multiplicación)
 *   - Verificación opcional: recomputa una muestra de celdas de C
 *     y las compara contra el resultado del algoritmo
 *
 * Compilación (Linux / WSL), con la bandera base decidida en el laboratorio 1:
 *   gcc -O3 matmul_secuencial_transpuesta.c -o matmul_secuencial_transpuesta
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/resource.h>

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

/* Llena la matriz con enteros aleatorios en [0, limite]. */
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
 * se guarda en bt[j][i]. Se recorre i, j en el mismo orden que
 * llenar_aleatorio para consumir rand() en la misma secuencia, de modo que
 * la matriz lógica B sea idéntica a la de la versión sin transponer.
 * Esta escritura con salto ocurre una sola vez (O(n^2)) y fuera de la
 * medición de tiempo de la multiplicación.
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
 * Multiplicación clásica O(n^3): C = A x B, secuencial.
 * Recibe BT (B transpuesta): el producto punto recorre la fila i de A y la
 * fila j de BT, ambas de forma contigua en memoria (fila x fila).
 */
static void multiplicar(long long **a, long long **bt, long long **c, int n)
{
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
 * y recomputa cada producto punto de forma independiente.
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
        fprintf(stderr, "Uso: %s <tamaño> [limite] [verificar] [semilla]\n", argv[0]);
        fprintf(stderr, "  tamaño >= %d, limite >= 0, verificar = 0|1\n", MIN_SIZE);
        return EXIT_FAILURE;
    }

    int n = atoi(argv[1]);
    int limite = (argc > 2) ? atoi(argv[2]) : 100;
    int verif = (argc > 3) ? atoi(argv[3]) : 1;
    unsigned int semilla = (argc > 4) ? (unsigned int)strtoul(argv[4], NULL, 10)
                                      : (unsigned int)time(NULL);

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

    /* Mide tiempo de pared transcurrido y CPU de usuario/sistema del proceso.
     * Al ser secuencial, RUSAGE_SELF es suficiente. */
    struct rusage uso_inicio, uso_fin;
    struct timespec reloj_inicio, reloj_fin;
    if (getrusage(RUSAGE_SELF, &uso_inicio) != 0)
    {
        perror("Error al iniciar la medicion del tiempo de usuario");
        liberar_matriz(A);
        liberar_matriz(BT);
        liberar_matriz(C);
        return EXIT_FAILURE;
    }
    clock_gettime(CLOCK_MONOTONIC, &reloj_inicio);
    multiplicar(A, BT, C, n);
    clock_gettime(CLOCK_MONOTONIC, &reloj_fin);
    if (getrusage(RUSAGE_SELF, &uso_fin) != 0)
    {
        perror("Error al finalizar la medicion del tiempo de usuario");
        liberar_matriz(A);
        liberar_matriz(BT);
        liberar_matriz(C);
        return EXIT_FAILURE;
    }
    double segundos_pared =
        (double)(reloj_fin.tv_sec - reloj_inicio.tv_sec) +
        (double)(reloj_fin.tv_nsec - reloj_inicio.tv_nsec) / 1e9;
    double segundos_usuario =
        (double)(uso_fin.ru_utime.tv_sec - uso_inicio.ru_utime.tv_sec) +
        (double)(uso_fin.ru_utime.tv_usec - uso_inicio.ru_utime.tv_usec) / 1e6;
    double segundos_sistema =
        (double)(uso_fin.ru_stime.tv_sec - uso_inicio.ru_stime.tv_sec) +
        (double)(uso_fin.ru_stime.tv_usec - uso_inicio.ru_stime.tv_usec) / 1e6;

    /* Solo se reportan métricas; nunca se imprimen las matrices */
    printf("Multiplicacion de matrices %dx%d completada.\n", n, n);
    printf("Limite de valores: %d | Semilla: %u | B transpuesta (fila x fila)\n", limite, semilla);
    printf("Tiempo de pared: %.3f segundos\n", segundos_pared);
    printf("Tiempo de usuario: %.3f segundos\n", segundos_usuario);
    printf("Tiempo de sistema: %.3f segundos\n", segundos_sistema);

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
