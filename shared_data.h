// File: shared_data.h

#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <pthread.h> // para mutex y variables de condicion
#include <semaphore.h> 
#include <stdbool.h> 

// -- constantes de configuracion del sistema --
#define MAX_BELTS 20        // numero maximo de bandas que podemos crear
#define MAX_INGREDIENTS 10  // maximo de tipos de ingredientes distintos
#define MAX_ORDERS_IN_QUEUE 50 // tamano del buffer de ordenes en espera


// enumeracion para los posibles estados de una banda
typedef enum {
    IDLE,      // esperando por una orden
    PREPARING, // preparando activamente una hamburguesa
    PAUSED,    // pausada por el usuario
    NO_INGREDIENTS // esperando por falta de ingredientes
} BeltStatus;

// enumeracion para identificar los ingredientes por un indice
typedef enum {
    BUN,       // pan
    PATTY,     // carne
    LETTUCE,   // lechuga
    TOMATO,    // tomate
    ONION,     // cebolla
    CHEESE,    // queso

} IngredientType;

// estructura que representa una sola orden de hamburguesa
typedef struct {
    unsigned int order_id; // identificador unico de la orden

    // array que indica cuantos de cada ingrediente se necesita
    int ingredients_needed[MAX_INGREDIENTS];
} BurgerOrder;

// estructura para manejar cada tipo de ingrediente
typedef struct {
    char name[20];
    int count; // cantidad disponible
    pthread_mutex_t mutex; // mutex para proteger el acceso a 'count'
} Ingredient;


// estructura que representa el estado de una banda de preparacion
typedef struct {
    pid_t pid; // process id de la banda
    BeltStatus status; // el estado actual (idle, preparing, etc)
    unsigned int burgers_processed; // contador de hamburguesas completadas
    unsigned int current_order_id; // id de la orden que esta procesando
    bool running; // bandera de control (actualmente no usada, se usa system_running)
} PreparationBelt;

// estructura para la cola circular de ordenes pendientes
typedef struct {
    BurgerOrder orders[MAX_ORDERS_IN_QUEUE];
    int head; // indice del proximo elemento a ser consumido
    int tail; // indice donde se insertara el proximo elemento
    int count; // numero actual de elementos en la cola
    pthread_mutex_t mutex; // mutex para proteger toda la cola
} OrderQueue;


// -- la estructura principal que se almacena en memoria compartida --
// contiene todo el estado del sistema
typedef struct {

    bool system_running; // bandera global para indicar a los procesos que deben terminar
    Ingredient ingredients[MAX_INGREDIENTS]; // inventario de ingredientes
    PreparationBelt belts[MAX_BELTS]; // array con el estado de todas las bandas
    int num_belts; // numero de bandas activas
    OrderQueue waiting_orders; // la cola de ordenes pendientes


    // semaforo que cuenta cuantas ordenes hay disponibles para procesar
    sem_t sem_orders_available;

    // semaforo que cuenta cuantos espacios libres hay en la cola de ordenes
    sem_t sem_space_available;

} SharedSystemState;


// nombre del objeto de memoria compartida
#define SHM_NAME "/burger_machine_shm"

#endif // SHARED_DATA_H