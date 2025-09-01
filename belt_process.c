// File: belt_process.c (Versión Corregida para Bloqueo por Ingredientes)

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>

#include "shared_data.h"

static SharedSystemState *shared_state = NULL;
static int belt_id;

// La función check_and_take_ingredients no necesita cambios.
bool check_and_take_ingredients(const BurgerOrder *order) {
    for (int i = 0; i < MAX_INGREDIENTS; i++) {
        if (order->ingredients_needed[i] > 0) {
            pthread_mutex_lock(&shared_state->ingredients[i].mutex);
        }
    }
    bool has_enough = true;
    for (int i = 0; i < MAX_INGREDIENTS; i++) {
        if (order->ingredients_needed[i] > 0) {
            if (shared_state->ingredients[i].count < order->ingredients_needed[i]) {
                has_enough = false;
                break; 
            }
        }
    }
    if (has_enough) {
        for (int i = 0; i < MAX_INGREDIENTS; i++) {
            if (order->ingredients_needed[i] > 0) {
                shared_state->ingredients[i].count -= order->ingredients_needed[i];
            }
        }
    }
    for (int i = MAX_INGREDIENTS - 1; i >= 0; i--) {
        if (order->ingredients_needed[i] > 0) {
            pthread_mutex_unlock(&shared_state->ingredients[i].mutex);
        }
    }
    return has_enough;
}

void start_belt_process(int id, const char* shm_name) {
    belt_id = id;
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) { perror("shm_open (belt)"); exit(1); }
    shared_state = mmap(NULL, sizeof(SharedSystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_state == MAP_FAILED) { perror("mmap (belt)"); exit(1); }
    close(shm_fd); 

    printf("[Banda %d, PID %d] Conectada y lista.\n", belt_id, getpid());

    while (shared_state->system_running) {
        if (shared_state->belts[belt_id].status == PAUSED) {
            sleep(1);
            continue;
        }
        
        shared_state->belts[belt_id].status = IDLE;
        shared_state->belts[belt_id].current_order_id = 0;

        printf("[Banda %d] Esperando una orden...\n", belt_id);
        sem_wait(&shared_state->sem_orders_available);

        if (!shared_state->system_running) {
            break;
        }

        BurgerOrder current_order;
        // Solo "espiamos" la orden, no la sacamos de la cola todavía.
        pthread_mutex_lock(&shared_state->waiting_orders.mutex);
        current_order = shared_state->waiting_orders.orders[shared_state->waiting_orders.head];
        pthread_mutex_unlock(&shared_state->waiting_orders.mutex);

        // Guardamos el ID de la orden que estamos intentando procesar.
        shared_state->belts[belt_id].current_order_id = current_order.order_id;

        if (check_and_take_ingredients(&current_order)) {
            // Si tuvimos éxito, AHORA SÍ sacamos la orden de la cola.
            pthread_mutex_lock(&shared_state->waiting_orders.mutex);
            shared_state->waiting_orders.head = (shared_state->waiting_orders.head + 1) % MAX_ORDERS_IN_QUEUE;
            shared_state->waiting_orders.count--;
            pthread_mutex_unlock(&shared_state->waiting_orders.mutex);

            sem_post(&shared_state->sem_space_available);

            shared_state->belts[belt_id].status = PREPARING;
            printf("[Banda %d] Preparando orden #%u...\n", belt_id, current_order.order_id);
            sleep(2); 

            shared_state->belts[belt_id].burgers_processed++;
            printf("[Banda %d] Orden #%u completada. Total: %u.\n", belt_id, current_order.order_id, shared_state->belts[belt_id].burgers_processed);
        } else {

            shared_state->belts[belt_id].status = NO_INGREDIENTS;
            printf("[Banda %d] Faltan ingredientes para la orden #%u. Pausando...\n", belt_id, current_order.order_id);

            // y volverá a intentar procesar esta misma orden después del sleep.

            sleep(3); // Esperamos un tiempo para no saturar el sistema re-intentando.
        }
    }

    printf("[Banda %d, PID %d] Terminando...\n", belt_id, getpid());
    munmap(shared_state, sizeof(SharedSystemState));
}