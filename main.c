// File: main.c (Versión Final y Completa)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      // Para fork(), getopt(), sleep()
#include <sys/mman.h>    // Para mmap(), shm_open()
#include <sys/stat.h>    // Para constantes de modo
#include <fcntl.h>       // Para constantes O_*
#include <signal.h>      // Para manejo de señales (Ctrl+C)
#include <sys/wait.h>    // Para wait()

#include "shared_data.h" // Nuestra definición de estructuras compartidas

// --- Prototipos de funciones de otros módulos que llamaremos ---
void start_belt_process(int belt_id, const char* shm_name);
void start_order_generator_process(const char* shm_name);
void start_ui_control_process(const char* shm_name);


// --- Variables Globales ---
// Puntero al estado compartido. Se hace global para que el manejador de señales pueda acceder a él.
SharedSystemState *shared_state = NULL;
// File descriptor de la memoria compartida.
int shm_fd = -1;

// --- Funciones de Limpieza y Manejo de Señales ---

// Función de limpieza. Se asegura de que todos los recursos se liberen.
void cleanup() {
    // La UI puede haber dejado la terminal en un estado no estándar.
    // Esto es un intento de restaurarla si el proceso principal hace la limpieza.
    printf("\033[?1049l"); // Salir de la pantalla alternativa
    printf("\033c");      // Resetear terminal

    if (shared_state != NULL) {
        printf("\n[Main] Iniciando limpieza de recursos...\n");

        // 1. Destruir semáforos
        sem_destroy(&shared_state->sem_orders_available);
        sem_destroy(&shared_state->sem_space_available);

        // 2. Destruir mutex de la cola
        pthread_mutex_destroy(&shared_state->waiting_orders.mutex);

        // 3. Destruir mutex de los ingredientes
        for (int i = 0; i < MAX_INGREDIENTS; ++i) {
            // Solo destruir si el ingrediente fue inicializado
            if (strlen(shared_state->ingredients[i].name) > 0) {
                pthread_mutex_destroy(&shared_state->ingredients[i].mutex);
            }
        }

        // 4. Desmapear la memoria compartida del espacio de direcciones del proceso
        munmap(shared_state, sizeof(SharedSystemState));
        shared_state = NULL;
    }

    if (shm_fd != -1) {
        // 5. Cerrar el file descriptor
        close(shm_fd);
        shm_fd = -1;
    }

    // 6. ¡MUY IMPORTANTE! Eliminar el objeto de memoria compartida del sistema.
    shm_unlink(SHM_NAME);

    printf("[Main] Limpieza completada.\n");
}

// Manejador de la señal SIGINT (generada por Ctrl+C).
void signal_handler(int sig) {
    if (sig == SIGINT) {
        if (shared_state != NULL) {
            // Esta es la bandera que todos los procesos hijos mirarán para saber si deben terminar.
            // Es volátil para asegurar que el cambio sea visible a través de los procesos.
            if(shared_state->system_running) {
                printf("\n[Main] Ctrl+C detectado. Señalizando terminación a todos los procesos...\n");
                shared_state->system_running = false;
            }
        }
    }
}


// --- Función Principal ---

int main(int argc, char *argv[]) {
    int num_belts = 5; // Valor por defecto

    // --- 1. Validación y Parseo de Parámetros de Línea de Comando ---
    if (argc > 1) {
        num_belts = atoi(argv[1]);
        if (num_belts <= 0 || num_belts > MAX_BELTS) {
            fprintf(stderr, "Error: El número de bandas debe estar entre 1 y %d.\n", MAX_BELTS);
            return 1;
        }
    }
    printf("[Main] Iniciando sistema con %d bandas de preparación.\n", num_belts);

    // Limpieza previa por si una ejecución anterior falló
    shm_unlink(SHM_NAME);

    // --- 2. Creación de la Memoria Compartida ---
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(1);
    }
    if (ftruncate(shm_fd, sizeof(SharedSystemState)) == -1) {
        perror("ftruncate");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        exit(1);
    }
    shared_state = mmap(NULL, sizeof(SharedSystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_state == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        exit(1);
    }
    printf("[Main] Memoria compartida creada y mapeada correctamente.\n");

    // --- 3. Inicialización de Datos y Sincronización ---
    printf("[Main] Inicializando estado del sistema y primitivas de sincronización...\n");
    
    memset(shared_state, 0, sizeof(SharedSystemState)); // Limpiar la memoria compartida
    shared_state->system_running = true;
    shared_state->num_belts = num_belts;

    // Inicializar ingredientes
    strcpy(shared_state->ingredients[BUN].name, "Pan"); shared_state->ingredients[BUN].count = 50;
    strcpy(shared_state->ingredients[PATTY].name, "Carne"); shared_state->ingredients[PATTY].count = 40;
    strcpy(shared_state->ingredients[LETTUCE].name, "Lechuga"); shared_state->ingredients[LETTUCE].count = 100;
    strcpy(shared_state->ingredients[TOMATO].name, "Tomate"); shared_state->ingredients[TOMATO].count = 80;
    strcpy(shared_state->ingredients[ONION].name, "Cebolla"); shared_state->ingredients[ONION].count = 90;
    strcpy(shared_state->ingredients[CHEESE].name, "Queso"); shared_state->ingredients[CHEESE].count = 60;

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);

    for (int i = 0; i < MAX_INGREDIENTS; ++i) {
        if(strlen(shared_state->ingredients[i].name) > 0) {
            pthread_mutex_init(&shared_state->ingredients[i].mutex, &mutex_attr);
        }
    }
    
    pthread_mutex_init(&shared_state->waiting_orders.mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    sem_init(&shared_state->sem_orders_available, 1, 0);
    sem_init(&shared_state->sem_space_available, 1, MAX_ORDERS_IN_QUEUE);

    signal(SIGINT, signal_handler);

    // --- 4. Creación de Procesos Hijos ---
    printf("[Main] Creando procesos hijos...\n");
    
    int total_child_processes = num_belts + 2; // N bandas + 1 generador + 1 UI/Control
    pid_t pids[total_child_processes];

    // Crear procesos de banda
    for (int i = 0; i < num_belts; ++i) {
        pids[i] = fork();
        if (pids[i] < 0) { perror("fork para banda"); exit(1); }
        if (pids[i] == 0) { // Proceso Hijo (Banda)
            start_belt_process(i, SHM_NAME); 
            exit(0);
        } else { // Proceso Padre
            shared_state->belts[i].pid = pids[i];
        }
    }
    
    // Crear proceso generador de órdenes
    pids[num_belts] = fork();
    if (pids[num_belts] < 0) { perror("fork para generador"); exit(1); }
    if (pids[num_belts] == 0) { // Proceso Hijo (Generador)
        start_order_generator_process(SHM_NAME);
        exit(0);
    }

    // Crear proceso de UI y Control
    pids[num_belts + 1] = fork();
    if (pids[num_belts + 1] < 0) { perror("fork para ui/control"); exit(1); }
    if (pids[num_belts + 1] == 0) { // Proceso Hijo (UI/Control)
        start_ui_control_process(SHM_NAME);
        exit(0);
    }

    printf("[Main] Todos los procesos han sido creados. El sistema está operativo.\n");
    printf("[Main] La interfaz de control está activa en esta terminal.\n");

    // --- 5. Espera y Terminación ---
    // El proceso padre espera aquí a que todos sus hijos terminen.
    // La llamada wait() es bloqueante, por lo que el padre no consumirá CPU.
    printf("[Main] Esperando la terminación de los procesos hijos (Ctrl+C para salir)...\n");
    for (int i = 0; i < total_child_processes; ++i) {
        wait(NULL);
    }

    // --- 6. Limpieza Final ---
    // Esta sección solo se alcanzará después de que todos los hijos hayan terminado.
    cleanup();

    return 0;
}