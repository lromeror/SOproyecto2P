// File: shared_data.h

#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <pthread.h>
#include <semaphore.h> 
#include <stdbool.h> 

// constantes de configuracion del sistema
#define MAX_BELTS 20
#define MAX_INGREDIENTS 10
#define MAX_ORDERS_IN_QUEUE 50


// representa los posibles estados de una banda
typedef enum {
    IDLE,
    PREPARING,
    PAUSED,
    NO_INGREDIENTS
} BeltStatus;

// indices para el array de ingredientes
typedef enum {
    BUN,
    PATTY,
    LETTUCE,
    TOMATO,
    ONION,
    CHEESE,
} IngredientType;

// define una orden de hamburguesa
typedef struct {
    unsigned int order_id;
    int ingredients_needed[MAX_INGREDIENTS];
} BurgerOrder;

// define el inventario de un tipo de ingrediente
typedef struct {
    char name[20];
    int count;
    pthread_mutex_t mutex;
} Ingredient;

// representa el estado de una banda de preparacion
typedef struct {
    pid_t pid;
    BeltStatus status;
    unsigned int burgers_processed;
    unsigned int current_order_id;
    bool running;
} PreparationBelt;

// cola circular para las ordenes en espera
typedef struct {
    BurgerOrder orders[MAX_ORDERS_IN_QUEUE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
} OrderQueue;


// estructura principal que se aloja en la memoria compartida
// contiene todo el estado del sistema
typedef struct {
    bool system_running;
    Ingredient ingredients[MAX_INGREDIENTS];
    PreparationBelt belts[MAX_BELTS];
    int num_belts;
    OrderQueue waiting_orders;

    // semaforo para ordenes disponibles
    sem_t sem_orders_available;

    // semaforo para espacios disponibles en la cola
    sem_t sem_space_available;

} SharedSystemState;


// nombre para el segmento de memoria compartida
#define SHM_NAME "/burger_machine_shm"

#endif 