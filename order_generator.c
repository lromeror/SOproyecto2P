// File: order_generator.c

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h> // Para la semilla del generador de números aleatorios

#include "shared_data.h"

// --- Variables Globales (solo para este proceso) ---
static SharedSystemState *shared_state = NULL;

// --- Función Principal del Proceso Generador de Órdenes ---

void start_order_generator_process(const char *shm_name)
{
    // --- 1. Conectarse a la Memoria Compartida ---
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("shm_open (generator)");
        exit(1);
    }

    shared_state = mmap(NULL, sizeof(SharedSystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_state == MAP_FAILED)
    {
        perror("mmap (generator)");
        exit(1);
    }
    close(shm_fd);

    // Semilla para el generador de números aleatorios.
    // Usamos el PID para asegurarnos de que sea único por proceso.
    srand(time(NULL) ^ getpid());

    printf("[Generator, PID %d] Conectado y listo para crear órdenes.\n", getpid());

    unsigned int order_counter = 0;

    // --- 2. Bucle Principal de Generación ---
    while (shared_state->system_running)
    {
        // Simular que las órdenes llegan a intervalos variables.
        sleep((rand() % 3) + 1); // Esperar entre 1 y 3 segundos.

        // **Concepto S.O. (Silberschatz): Sincronización (Problema Productor-Consumidor)**
        // `sem_wait` es la operación del productor antes de producir. El proceso se bloqueará
        // aquí si la cola está llena (si el valor del semáforo de espacio es 0).
        // Esto previene un desbordamiento del buffer (la cola).
        sem_wait(&shared_state->sem_space_available);

        // Chequeo de salida por si nos despertaron para terminar.
        if (!shared_state->system_running)
        {
            break;
        }

        // --- Crear una nueva orden ---
        BurgerOrder new_order;
        new_order.order_id = ++order_counter;

        // --- CÓDIGO MODIFICADO PARA ÓRDENES SIEMPRE POSIBLES ---
        // Todas las hamburguesas tienen pan y carne.
        new_order.ingredients_needed[BUN] = 2;
        new_order.ingredients_needed[PATTY] = 1;

        // Añadir otros ingredientes de forma aleatoria, pero con límites razonables.
        // Esto asegura que cada orden individual sea siempre "posible" al principio.
        new_order.ingredients_needed[LETTUCE] = (rand() % 100 < 80) ? 1 : 0; // 80% de probabilidad de querer 1 de lechuga
        new_order.ingredients_needed[TOMATO] = (rand() % 100 < 70) ? 1 : 0;  // 70% de probabilidad de querer 1 de tomate
        new_order.ingredients_needed[ONION] = (rand() % 100 < 60) ? 1 : 0;   // 60% de probabilidad de querer 1 de cebolla
        new_order.ingredients_needed[CHEESE] = (rand() % 100 < 90) ? 1 : 0;  // 90% de probabilidad de querer 1 de queso

        // Aseguramos que los demás ingredientes (no usados en esta lógica simple) se pidan en cantidad 0.
        for (int i = CHEESE + 1; i < MAX_INGREDIENTS; i++)
        {
            new_order.ingredients_needed[i] = 0;
        }
        // --- FIN DEL CÓDIGO MODIFICADO ---

        // **Concepto S.O. (Silberschatz): Sección Crítica**
        // Bloqueamos el mutex para manipular la estructura de la cola de forma segura.
        pthread_mutex_lock(&shared_state->waiting_orders.mutex);

        // Añadir la orden a la cola en la posición `tail`.
        int tail_pos = shared_state->waiting_orders.tail;
        shared_state->waiting_orders.orders[tail_pos] = new_order;
        shared_state->waiting_orders.tail = (tail_pos + 1) % MAX_ORDERS_IN_QUEUE;
        shared_state->waiting_orders.count++;

        printf("[Generator] Nueva orden #%u creada. Total en cola: %d\n", new_order.order_id, shared_state->waiting_orders.count);

        // Liberamos el mutex.
        pthread_mutex_unlock(&shared_state->waiting_orders.mutex);

        // **Concepto S.O. (Silberschatz): Señalización**
        // Ahora que hemos añadido una orden, debemos notificar a los consumidores (bandas).
        // `sem_post` incrementa el contador de `sem_orders_available`. Si alguna banda
        // estaba dormida en `sem_wait`, esta llamada la despertará.
        sem_post(&shared_state->sem_orders_available);
    }

    // --- 3. Limpieza ---
    printf("[Generator, PID %d] Terminando...\n", getpid());
    munmap(shared_state, sizeof(SharedSystemState));
}