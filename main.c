#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   
#include <sys/mman.h> 
#include <sys/stat.h> 
#include <fcntl.h>   
#include <signal.h>  
#include <sys/wait.h> 

#include "shared_data.h"

// prototipos de las funciones que inician los otros procesos
void start_belt_process(int belt_id, const char *shm_name);
void start_order_generator_process(const char *shm_name);
void start_ui_control_process(const char *shm_name);

// puntero global a la memoria compartida
SharedSystemState *shared_state = NULL;

// descriptor de archivo para la memoria compartida
int shm_fd = -1;

// funcion para despertar a los hijos que puedan estar bloqueados en un semaforo
void wake_up_children()
{
    if (shared_state == NULL)
        return;
    // despertar a todas las bandas posibles
    for (int i = 0; i < shared_state->num_belts; i++)
    {
        sem_post(&shared_state->sem_orders_available);
    }
    // despertar al generador de ordenes
    sem_post(&shared_state->sem_space_available);
}

// funcion para liberar todos los recursos al terminar
void cleanup()
{
    // restauramos la terminal por si algo salio mal
    printf("\033[?1049l");
    printf("\033c");

    if (shared_state != NULL)
    {
        printf("\n[Main] Iniciando limpieza de recursos...\n");
        // destruimos semaforos y mutex
        sem_destroy(&shared_state->sem_orders_available);
        sem_destroy(&shared_state->sem_space_available);
        pthread_mutex_destroy(&shared_state->waiting_orders.mutex);
        for (int i = 0; i < MAX_INGREDIENTS; ++i)
        {
            if (strlen(shared_state->ingredients[i].name) > 0)
            {
                pthread_mutex_destroy(&shared_state->ingredients[i].mutex);
            }
        }
        // liberamos el mapeo de memoria
        munmap(shared_state, sizeof(SharedSystemState));
        shared_state = NULL;
    }
    if (shm_fd != -1)
    {
        // cerramos el descriptor de archivo
        close(shm_fd);
        shm_fd = -1;
    }
    // eliminamos el archivo de memoria compartida
    shm_unlink(SHM_NAME);
    printf("[Main] Limpieza completada.\n");
}

// manejador para la senal de ctrl+c
void signal_handler(int sig)
{
    if (sig == SIGINT)
    {
        if (shared_state != NULL && shared_state->system_running)
        {
            printf("\n[Main] Ctrl+C detectado. Senalizando terminacion...\n");
            // avisamos a todos los hijos que deben terminar
            shared_state->system_running = false; 

            // despertamos a los hijos por si estan dormidos en un sem_wait
            wake_up_children();

            // pausa critica para dar tiempo a la ui de restaurar la terminal
            sleep(1);
        }
    }
}

int main(int argc, char *argv[])
{
    int num_belts = 5;
    if (argc > 1)
    {
        num_belts = atoi(argv[1]);
        if (num_belts <= 0 || num_belts > MAX_BELTS)
        {
            fprintf(stderr, "Error: El numero de bandas debe estar entre 1 y %d.\n", MAX_BELTS);
            return 1;
        }
    }
    printf("[Main] Iniciando sistema con %d bandas de preparacion.\n", num_belts);

    // preparamos la memoria compartida
    shm_unlink(SHM_NAME);
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("shm_open");
        exit(1);
    }
    if (ftruncate(shm_fd, sizeof(SharedSystemState)) == -1)
    {
        perror("ftruncate");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        exit(1);
    }
    shared_state = mmap(NULL, sizeof(SharedSystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_state == MAP_FAILED)
    {
        perror("mmap");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        exit(1);
    }
    printf("[Main] Memoria compartida creada y mapeada correctamente.\n");

    // inicializamos el estado del sistema
    printf("[Main] Inicializando estado del sistema y primitivas de sincronizacion...\n");
    memset(shared_state, 0, sizeof(SharedSystemState));
    shared_state->system_running = true;
    shared_state->num_belts = num_belts;

    // definimos los ingredientes iniciales
    strcpy(shared_state->ingredients[BUN].name, "Pan");
    shared_state->ingredients[BUN].count = 50;
    strcpy(shared_state->ingredients[PATTY].name, "Carne");
    shared_state->ingredients[PATTY].count = 40;
    strcpy(shared_state->ingredients[LETTUCE].name, "Lechuga");
    shared_state->ingredients[LETTUCE].count = 100;
    strcpy(shared_state->ingredients[TOMATO].name, "Tomate");
    shared_state->ingredients[TOMATO].count = 80;
    strcpy(shared_state->ingredients[ONION].name, "Cebolla");
    shared_state->ingredients[ONION].count = 90;
    strcpy(shared_state->ingredients[CHEESE].name, "Queso");
    shared_state->ingredients[CHEESE].count = 60;

    // inicializamos mutex y semaforos
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    for (int i = 0; i < MAX_INGREDIENTS; ++i)
    {
        if (strlen(shared_state->ingredients[i].name) > 0)
        {
            pthread_mutex_init(&shared_state->ingredients[i].mutex, &mutex_attr);
        }
    }
    pthread_mutex_init(&shared_state->waiting_orders.mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);
    sem_init(&shared_state->sem_orders_available, 1, 0);
    sem_init(&shared_state->sem_space_available, 1, MAX_ORDERS_IN_QUEUE);

    // registramos el manejador de senales
    signal(SIGINT, signal_handler);

    // creamos todos los procesos hijos
    printf("[Main] Creando procesos hijos...\n");
    int total_child_processes = num_belts + 2;
    pid_t pids[total_child_processes];
    for (int i = 0; i < num_belts; ++i)
    {
        pids[i] = fork();
        if (pids[i] < 0)
        {
            perror("fork para banda");
            exit(1);
        }
        if (pids[i] == 0)
        {
            start_belt_process(i, SHM_NAME);
            exit(0);
        }
        else
        {
            shared_state->belts[i].pid = pids[i];
        }
    }
    pids[num_belts] = fork();
    if (pids[num_belts] < 0)
    {
        perror("fork para generador");
        exit(1);
    }
    if (pids[num_belts] == 0)
    {
        start_order_generator_process(SHM_NAME);
        exit(0);
    }
    pids[num_belts + 1] = fork();
    if (pids[num_belts + 1] < 0)
    {
        perror("fork para ui/control");
        exit(1);
    }
    if (pids[num_belts + 1] == 0)
    {
        start_ui_control_process(SHM_NAME);
        exit(0);
    }
    printf("[Main] Todos los procesos han sido creados. El sistema esta operativo.\n");
    printf("[Main] La interfaz de control esta activa en esta terminal.\n");
    
    // el proceso padre se queda esperando a que terminen los hijos
    printf("[Main] Esperando la terminacion de los procesos hijos (Ctrl+C para salir)...\n");
    for (int i = 0; i < total_child_processes; ++i)
    {
        wait(NULL);
    }
    
    // una vez que todos terminan, limpiamos
    cleanup();
    
    return 0;
}