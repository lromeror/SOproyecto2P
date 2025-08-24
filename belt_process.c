// File: belt_process.c

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>

#include "shared_data.h"

// --- Variables Globales (solo para este proceso) ---
static SharedSystemState *shared_state = NULL;
static int belt_id;

// --- Funciones Auxiliares ---

// Función CRÍTICA: Intenta verificar y tomar los ingredientes de forma atómica.
// Devuelve `true` si tuvo éxito, `false` si no hay suficientes ingredientes.
bool check_and_take_ingredients(const BurgerOrder *order) {
    // **Concepto S.O. (Silberschatz): Prevención de Deadlock**
    // Para evitar un deadlock (Banda1 toma Pan y espera por Carne, Banda2 toma Carne y espera por Pan),
    // siempre adquirimos los locks de los mutex en el mismo orden (ascendente por ID de ingrediente).
    for (int i = 0; i < MAX_INGREDIENTS; i++) {
        if (order->ingredients_needed[i] > 0) {
            pthread_mutex_lock(&shared_state->ingredients[i].mutex);
        }
    }

    // **Concepto S.O. (Silberschatz): Sección Crítica**
    // Ahora que tenemos todos los locks, estamos en una sección crítica.
    // Ninguna otra banda puede modificar el inventario de los ingredientes que necesitamos.
    
    bool has_enough = true;
    // Primera pasada: solo verificar si tenemos suficiente de todo.
    for (int i = 0; i < MAX_INGREDIENTS; i++) {
        if (order->ingredients_needed[i] > 0) {
            if (shared_state->ingredients[i].count < order->ingredients_needed[i]) {
                has_enough = false;
                break; // Salir tan pronto como encontremos un ingrediente faltante.
            }
        }
    }

    // Si tenemos todo, procedemos a tomar los ingredientes (decrementarlos).
    if (has_enough) {
        for (int i = 0; i < MAX_INGREDIENTS; i++) {
            if (order->ingredients_needed[i] > 0) {
                shared_state->ingredients[i].count -= order->ingredients_needed[i];
            }
        }
    }
    
    // **Fin de la Sección Crítica**
    // Liberamos todos los locks que tomamos, en el orden inverso por buena práctica.
    for (int i = MAX_INGREDIENTS - 1; i >= 0; i--) {
        if (order->ingredients_needed[i] > 0) {
            pthread_mutex_unlock(&shared_state->ingredients[i].mutex);
        }
    }
    
    return has_enough;
}


// --- Función Principal del Proceso de Banda ---

void start_belt_process(int id, const char* shm_name) {
    belt_id = id;

    // --- 1. Conectarse a la Memoria Compartida ---
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open (belt)");
        exit(1);
    }

    shared_state = mmap(NULL, sizeof(SharedSystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_state == MAP_FAILED) {
        perror("mmap (belt)");
        exit(1);
    }
    close(shm_fd); // Ya no necesitamos el file descriptor después de mmap

    printf("[Banda %d, PID %d] Conectada y lista.\n", belt_id, getpid());

    // --- 2. Bucle Principal de Trabajo ---
    while (shared_state->system_running) {
        
        // --- Manejo del Estado de Pausa ---
        if (shared_state->belts[belt_id].status == PAUSED) {
            sleep(1); // Si está pausada, simplemente espera.
            continue; // Vuelve al inicio del bucle.
        }
        
        shared_state->belts[belt_id].status = IDLE;
        
        // **Concepto S.O. (Silberschatz): Sincronización (Problema Productor-Consumidor)**
        // `sem_wait` es la operación del consumidor. El proceso se bloqueará aquí (dormirá)
        // si el valor del semáforo es 0 (no hay órdenes). Cuando un productor haga `sem_post`,
        // el semáforo se incrementará y este proceso se despertará.
        printf("[Banda %d] Esperando una orden...\n", belt_id);
        sem_wait(&shared_state->sem_orders_available);

        // Chequeo de salida después de despertar, por si nos despertaron para terminar.
        if (!shared_state->system_running) {
            break;
        }

        // --- 3. Intentar Procesar la Orden ---
        // Miramos la orden que está al frente de la cola sin sacarla todavía.
        BurgerOrder current_order;
        pthread_mutex_lock(&shared_state->waiting_orders.mutex);
        current_order = shared_state->waiting_orders.orders[shared_state->waiting_orders.head];
        pthread_mutex_unlock(&shared_state->waiting_orders.mutex);

        // Intentar obtener los ingredientes.
        if (check_and_take_ingredients(&current_order)) {
            // ¡Éxito! Tenemos los ingredientes. Ahora podemos sacar la orden de la cola.
            
            // --- Sacar la orden de la cola (sección crítica) ---
            pthread_mutex_lock(&shared_state->waiting_orders.mutex);
            shared_state->waiting_orders.head = (shared_state->waiting_orders.head + 1) % MAX_ORDERS_IN_QUEUE;
            shared_state->waiting_orders.count--;
            pthread_mutex_unlock(&shared_state->waiting_orders.mutex);

            // Avisar al productor que hay un nuevo espacio libre en la cola.
            sem_post(&shared_state->sem_space_available);

            // --- Procesar la hamburguesa ---
            shared_state->belts[belt_id].status = PREPARING;
            shared_state->belts[belt_id].current_order_id = current_order.order_id;
            printf("[Banda %d] Preparando orden #%u...\n", belt_id, current_order.order_id);
            sleep(2); // Simular el tiempo de preparación

            shared_state->belts[belt_id].burgers_processed++;
            printf("[Banda %d] Orden #%u completada. Total: %u.\n", belt_id, current_order.order_id, shared_state->belts[belt_id].burgers_processed);

        } else {
            // No hay suficientes ingredientes para la orden del frente.
            shared_state->belts[belt_id].status = NO_INGREDIENTS;
            printf("[Banda %d] Faltan ingredientes para la orden #%u. Esperando...\n", belt_id, current_order.order_id);

            // **IMPORTANTE**: Como no consumimos la orden, debemos devolver el "ticket"
            // al semáforo para que otra banda pueda intentarlo o para que nosotros mismos
            // lo intentemos más tarde. Si no hacemos esto, el sistema se bloquea.
            sem_post(&shared_state->sem_orders_available);
            
            sleep(3); // Esperar un tiempo antes de volver a intentar, para no acaparar la CPU.
        }
    }

    // --- 4. Limpieza ---
    printf("[Banda %d, PID %d] Terminando...\n", belt_id, getpid());
    munmap(shared_state, sizeof(SharedSystemState));
    
    // La función termina, y el `exit(0)` en main.c se encargará del resto.
}