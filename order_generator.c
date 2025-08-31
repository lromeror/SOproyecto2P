// File: order_generator.c

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h> 

#include "shared_data.h"

// puntero a la estructura de estado compartida
static SharedSystemState *shared_state = NULL;



void start_order_generator_process(const char *shm_name)
{
    // --- 1. conectarse a la memoria compartida ---
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("shm_open (generator)");
        exit(1);
    }

    // mapeamos la memoria compartida a nuestro espacio de direcciones
    shared_state = mmap(NULL, sizeof(SharedSystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_state == MAP_FAILED)
    {
        perror("mmap (generator)");
        exit(1);
    }
    close(shm_fd); // ya no necesitamos el descriptor

    // inicializamos la semilla para el generador de numeros aleatorios
    // usamos el pid para que cada proceso generador (si hubiera mas) tenga una semilla diferente
    srand(time(NULL) ^ getpid());

    printf("[Generator, PID %d] Conectado y listo para crear ordenes.\n", getpid());

    unsigned int order_counter = 0;

    // --- 2. bucle principal de generacion ---
    while (shared_state->system_running)
    {
        // esperamos un tiempo aleatorio para simular la llegada de clientes
        sleep((rand() % 3) + 1); 

        // esperamos a que haya espacio disponible en la cola de ordenes
        // si la cola esta llena, este proceso se quedara bloqueado aqui
        sem_wait(&shared_state->sem_space_available);

        // al despertar, verificamos si es porque el sistema se esta apagando
        if (!shared_state->system_running)
        {
            break;
        }

        // creamos una nueva orden
        BurgerOrder new_order;
        new_order.order_id = ++order_counter;
        // ingredientes base que toda hamburguesa lleva
        new_order.ingredients_needed[BUN] = 2;
        new_order.ingredients_needed[PATTY] = 1;

        // ingredientes opcionales, se anaden con cierta probabilidad
        new_order.ingredients_needed[LETTUCE] = (rand() % 100 < 80) ? 1 : 0; 
        new_order.ingredients_needed[TOMATO] = (rand() % 100 < 70) ? 1 : 0; 
        new_order.ingredients_needed[ONION] = (rand() % 100 < 60) ? 1 : 0;  
        new_order.ingredients_needed[CHEESE] = (rand() % 100 < 90) ? 1 : 0;  

        // nos aseguramos de que los demas ingredientes esten en cero
        for (int i = CHEESE + 1; i < MAX_INGREDIENTS; i++)
        {
            new_order.ingredients_needed[i] = 0;
        }
    
        // bloqueamos el mutex para poder modificar la cola de ordenes de forma segura
        pthread_mutex_lock(&shared_state->waiting_orders.mutex);

        // anadimos la nueva orden al final de la cola (en la posicion 'tail')
        int tail_pos = shared_state->waiting_orders.tail;
        shared_state->waiting_orders.orders[tail_pos] = new_order;
        // movemos el puntero 'tail' a la siguiente posicion, de forma circular
        shared_state->waiting_orders.tail = (tail_pos + 1) % MAX_ORDERS_IN_QUEUE;
        shared_state->waiting_orders.count++;

        printf("[Generator] Nueva orden #%u creada. Total en cola: %d\n", new_order.order_id, shared_state->waiting_orders.count);

        // liberamos el mutex para que otros procesos puedan acceder a la cola
        pthread_mutex_unlock(&shared_state->waiting_orders.mutex);

        // incrementamos el semaforo de ordenes disponibles para 'despertar' a una banda
        sem_post(&shared_state->sem_orders_available);
    }

    printf("[Generator, PID %d] Terminando...\n", getpid());
    // liberamos la memoria mapeada antes de salir
    munmap(shared_state, sizeof(SharedSystemState));
}