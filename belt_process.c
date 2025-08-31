// File: belt_process.c

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


// funcion para verificar y tomar los ingredientes necesarios para una orden
bool check_and_take_ingredients(const BurgerOrder *order) {

    // primero, bloqueamos todos los mutex de los ingredientes que vamos a necesitar
    for (int i = 0; i < MAX_INGREDIENTS; i++) {
        if (order->ingredients_needed[i] > 0) {
            pthread_mutex_lock(&shared_state->ingredients[i].mutex);
        }
    }

    // partimos del supuesto que si tenemos todo
    bool has_enough = true;

    // revisamos si de verdad tenemos suficientes ingredientes para la orden
    for (int i = 0; i < MAX_INGREDIENTS; i++) {
        if (order->ingredients_needed[i] > 0) {
            if (shared_state->ingredients[i].count < order->ingredients_needed[i]) {
                has_enough = false;
                break; // si falta uno, ya no seguimos revisando
            }
        }
    }

    // si despues de revisar, confirmamos que si habia, los descontamos
    if (has_enough) {
        for (int i = 0; i < MAX_INGREDIENTS; i++) {
            if (order->ingredients_needed[i] > 0) {
                shared_state->ingredients[i].count -= order->ingredients_needed[i];
            }
        }
    }

    // liberamos los mutex en orden inverso para evitar deadlocks
    for (int i = MAX_INGREDIENTS - 1; i >= 0; i--) {
        if (order->ingredients_needed[i] > 0) {
            pthread_mutex_unlock(&shared_state->ingredients[i].mutex);
        }
    }
    
    return has_enough;
}



void start_belt_process(int id, const char* shm_name) {
    belt_id = id;

    // abrimos el descriptor de la memoria compartida
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open (belt)");
        exit(1);
    }

    // mapeamos la memoria compartida a nuestro espacio de memoria virtual
    shared_state = mmap(NULL, sizeof(SharedSystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_state == MAP_FAILED) {
        perror("mmap (belt)");
        exit(1);
    }
    close(shm_fd); // una vez mapeado, ya no necesitamos el descriptor

    printf("[Banda %d, PID %d] Conectada y lista.\n", belt_id, getpid());

    // bucle principal, se ejecuta mientras el sistema este corriendo
    while (shared_state->system_running) {
        
        // si la banda esta en pausa, solo esperamos y volvemos a empezar el ciclo
        if (shared_state->belts[belt_id].status == PAUSED) {
            sleep(1); 
            continue; 
        }
        
        // nos ponemos en modo inactivo (idle) mientras esperamos orden
        shared_state->belts[belt_id].status = IDLE;

        printf("[Banda %d] Esperando una orden...\n", belt_id);
        sem_wait(&shared_state->sem_orders_available); // esperamos a que el semaforo nos avise de una orden

        // al despertar, volvemos a chequear si el sistema debe terminar
        if (!shared_state->system_running) {
            break;
        }

        BurgerOrder current_order;
        // espiamos la orden que esta al frente de la cola, sin sacarla aun
        pthread_mutex_lock(&shared_state->waiting_orders.mutex);
        current_order = shared_state->waiting_orders.orders[shared_state->waiting_orders.head];
        pthread_mutex_unlock(&shared_state->waiting_orders.mutex);


        // intentamos tomar los ingredientes. si lo logramos...
        if (check_and_take_ingredients(&current_order)) {
  
            // ahora si, sacamos la orden de la cola de forma segura
            pthread_mutex_lock(&shared_state->waiting_orders.mutex);
            shared_state->waiting_orders.head = (shared_state->waiting_orders.head + 1) % MAX_ORDERS_IN_QUEUE;
            shared_state->waiting_orders.count--;
            pthread_mutex_unlock(&shared_state->waiting_orders.mutex);

            // avisamos que hay un nuevo espacio disponible en la cola de ordenes
            sem_post(&shared_state->sem_space_available);

            // actualizamos nuestro estado y empezamos a preparar
            shared_state->belts[belt_id].status = PREPARING;
            shared_state->belts[belt_id].current_order_id = current_order.order_id;
            printf("[Banda %d] Preparando orden #%u...\n", belt_id, current_order.order_id);
            sleep(2); // simulamos el tiempo que toma preparar la hamburguesa

            shared_state->belts[belt_id].burgers_processed++;
            printf("[Banda %d] Orden #%u completada. Total: %u.\n", belt_id, current_order.order_id, shared_state->belts[belt_id].burgers_processed);

        } else {
            // si no habia ingredientes suficientes...
            shared_state->belts[belt_id].status = NO_INGREDIENTS;
            printf("[Banda %d] Faltan ingredientes para la orden #%u. Esperando...\n", belt_id, current_order.order_id);

            // devolvemos la "notificacion" de orden para que otra banda lo intente
            sem_post(&shared_state->sem_orders_available);
            
            // esperamos un tiempo antes de volver a intentar procesar una orden
            sleep(3); 
        }
    }

    printf("[Banda %d, PID %d] Terminando...\n", belt_id, getpid());
    // liberamos la memoria mapeada antes de salir
    munmap(shared_state, sizeof(SharedSystemState));
    

}