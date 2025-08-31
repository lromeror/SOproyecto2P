// File: main.c (Version Final y Completa)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      // para fork(), getopt(), sleep()
#include <sys/mman.h>    // para mmap(), shm_open()
#include <sys/stat.h>    // para constantes de modo
#include <fcntl.h>       // para constantes o_*
#include <signal.h>      // para manejo de senales (ctrl+c)
#include <sys/wait.h>    // para wait()

#include "shared_data.h" 


// prototipos de las funciones que inician los otros procesos
void start_belt_process(int belt_id, const char* shm_name);
void start_order_generator_process(const char* shm_name);
void start_ui_control_process(const char* shm_name);


// puntero global a la memoria compartida
SharedSystemState *shared_state = NULL;

// descriptor de archivo para la memoria compartida
int shm_fd = -1;


// funcion para liberar todos los recursos al terminar
void cleanup() {

    printf("\033[?1049l"); // salir de la pantalla alternativa de la ui
    printf("\033c");      // resetear la terminal a su estado normal

    if (shared_state != NULL) {
        printf("\n[Main] Iniciando limpieza de recursos...\n");

        // destruimos los semaforos
        sem_destroy(&shared_state->sem_orders_available);
        sem_destroy(&shared_state->sem_space_available);

        // destruimos el mutex de la cola de ordenes
        pthread_mutex_destroy(&shared_state->waiting_orders.mutex);

        // recorremos los ingredientes para destruir sus mutex individuales
        for (int i = 0; i < MAX_INGREDIENTS; ++i) {
            if (strlen(shared_state->ingredients[i].name) > 0) {
                pthread_mutex_destroy(&shared_state->ingredients[i].mutex);
            }
        }

        // liberamos el mapeo de memoria
        munmap(shared_state, sizeof(SharedSystemState));
        shared_state = NULL;
    }

    if (shm_fd != -1) {
        // cerramos el descriptor de archivo
        close(shm_fd);
        shm_fd = -1;
    }

    // eliminamos el archivo de memoria compartida del sistema
    shm_unlink(SHM_NAME);

    printf("[Main] Limpieza completada.\n");
}


// manejador para la senal de ctrl+c (sigint)
void signal_handler(int sig) {
    if (sig == SIGINT) {
        if (shared_state != NULL) {
          
            // nos aseguramos de senalizar la terminacion solo una vez
            if(shared_state->system_running) {
                printf("\n[Main] Ctrl+C detectado. Senalizando terminacion a todos los procesos...\n");
                shared_state->system_running = false; // esta bandera hara que los bucles de los hijos terminen
            }
        }
    }
}



int main(int argc, char *argv[]) {
    int num_belts = 5; // valor por defecto si no se especifica

    // permite cambiar el numero de bandas por argumento en linea de comandos
    if (argc > 1) {
        num_belts = atoi(argv[1]);
        if (num_belts <= 0 || num_belts > MAX_BELTS) {
            fprintf(stderr, "Error: El numero de bandas debe estar entre 1 y %d.\n", MAX_BELTS);
            return 1;
        }
    }
    printf("[Main] Iniciando sistema con %d bandas de preparacion.\n", num_belts);

    // nos aseguramos de que no exista un segmento de memoria con el mismo nombre
    shm_unlink(SHM_NAME);

    // creamos el objeto de memoria compartida
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(1);
    }
    // establecemos el tamano del objeto de memoria compartida
    if (ftruncate(shm_fd, sizeof(SharedSystemState)) == -1) {
        perror("ftruncate");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        exit(1);
    }
    // mapeamos el objeto de memoria compartida al espacio de direcciones de este proceso
    shared_state = mmap(NULL, sizeof(SharedSystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_state == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        exit(1);
    }
    printf("[Main] Memoria compartida creada y mapeada correctamente.\n");


    printf("[Main] Inicializando estado del sistema y primitivas de sincronizacion...\n");
    
    // inicializamos toda la memoria compartida a ceros
    memset(shared_state, 0, sizeof(SharedSystemState)); 
    shared_state->system_running = true;
    shared_state->num_belts = num_belts;

    // definimos los ingredientes iniciales
    strcpy(shared_state->ingredients[BUN].name, "Pan"); shared_state->ingredients[BUN].count = 50;
    strcpy(shared_state->ingredients[PATTY].name, "Carne"); shared_state->ingredients[PATTY].count = 40;
    strcpy(shared_state->ingredients[LETTUCE].name, "Lechuga"); shared_state->ingredients[LETTUCE].count = 100;
    strcpy(shared_state->ingredients[TOMATO].name, "Tomate"); shared_state->ingredients[TOMATO].count = 80;
    strcpy(shared_state->ingredients[ONION].name, "Cebolla"); shared_state->ingredients[ONION].count = 90;
    strcpy(shared_state->ingredients[CHEESE].name, "Queso"); shared_state->ingredients[CHEESE].count = 60;

    // preparamos los atributos para que los mutex se puedan compartir entre procesos
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);

    // inicializamos un mutex por cada tipo de ingrediente
    for (int i = 0; i < MAX_INGREDIENTS; ++i) {
        if(strlen(shared_state->ingredients[i].name) > 0) {
            pthread_mutex_init(&shared_state->ingredients[i].mutex, &mutex_attr);
        }
    }
    
    pthread_mutex_init(&shared_state->waiting_orders.mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr); // ya no necesitamos los atributos

    // inicializamos los semaforos. el segundo argumento '1' indica que son compartidos entre procesos
    sem_init(&shared_state->sem_orders_available, 1, 0); // empieza en 0 porque no hay ordenes
    sem_init(&shared_state->sem_space_available, 1, MAX_ORDERS_IN_QUEUE); // empieza lleno de espacios

    // registramos el manejador de senales
    signal(SIGINT, signal_handler);


    printf("[Main] Creando procesos hijos...\n");
    
    int total_child_processes = num_belts + 2; // bandas + generador + ui
    pid_t pids[total_child_processes];

    // creamos todos los procesos para las bandas
    for (int i = 0; i < num_belts; ++i) {
        pids[i] = fork();
        if (pids[i] < 0) { perror("fork para banda"); exit(1); }
        if (pids[i] == 0) { // codigo que ejecuta el proceso hijo (banda)
            start_belt_process(i, SHM_NAME); 
            exit(0);
        } else { // codigo que ejecuta el proceso padre
            shared_state->belts[i].pid = pids[i];
        }
    }
    
    // creamos el proceso generador de ordenes
    pids[num_belts] = fork();
    if (pids[num_belts] < 0) { perror("fork para generador"); exit(1); }
    if (pids[num_belts] == 0) { // proceso hijo (generador)
        start_order_generator_process(SHM_NAME);
        exit(0);
    }

    // creamos el proceso de la interfaz de usuario y control
    pids[num_belts + 1] = fork();
    if (pids[num_belts + 1] < 0) { perror("fork para ui/control"); exit(1); }
    if (pids[num_belts + 1] == 0) { // proceso hijo (ui/control)
        start_ui_control_process(SHM_NAME);
        exit(0);
    }

    printf("[Main] Todos los procesos han sido creados. El sistema esta operativo.\n");
    printf("[Main] La interfaz de control esta activa en esta terminal.\n");

    // el proceso principal se queda esperando a que terminen todos los hijos
    printf("[Main] Esperando la terminacion de los procesos hijos (Ctrl+C para salir)...\n");
    for (int i = 0; i < total_child_processes; ++i) {
        wait(NULL);
    }

    // una vez que todos los hijos han terminado, se procede a limpiar
    cleanup();

    return 0;
}