// File: shared_data.h

#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <pthread.h> // Para mutex y variables de condición
#include <semaphore.h> // Para semáforos
#include <stdbool.h> // Para usar bool, true, false

// --- Constantes Configurables ---
#define MAX_BELTS 20         // Máximo número de bandas soportadas
#define MAX_INGREDIENTS 10   // Máximo tipo de ingredientes
#define MAX_ORDERS_IN_QUEUE 50 // Tamaño máximo de la cola de espera FIFO

// --- Enumeraciones para claridad ---
// Posibles estados de una banda de preparación
typedef enum {
    IDLE,      // Esperando una orden
    PREPARING, // Preparando una hamburguesa
    PAUSED,    // Pausada por el usuario
    NO_INGREDIENTS // No pudo tomar una orden por falta de ingredientes
} BeltStatus;

// Nombres de los ingredientes para fácil referencia
typedef enum {
    BUN,       // Pan
    PATTY,     // Carne
    LETTUCE,   // Lechuga
    TOMATO,    // Tomate
    ONION,     // Cebolla
    CHEESE,    // Queso
    // Se pueden añadir más si es necesario
} IngredientType;

// --- Estructuras de Datos ---

// Representa una orden de hamburguesa
typedef struct {
    unsigned int order_id;
    // Un array que indica cuántas unidades de cada ingrediente se necesita.
    // Ejemplo: ingredients_needed[TOMATO] = 2; significa 2 rodajas de tomate.
    int ingredients_needed[MAX_INGREDIENTS];
} BurgerOrder;

// Representa el estado de un solo dispensador de ingrediente
typedef struct {
    char name[20];
    int count; // Cantidad disponible
    pthread_mutex_t mutex; // ¡CRUCIAL! Un mutex por ingrediente para granularidad fina.
                           // Evita que todas las bandas se bloqueen si solo una va a por tomates.
} Ingredient;

// Representa el estado de una banda de preparación
typedef struct {
    pid_t pid; // PID del proceso que la controla, para poder enviarle señales
    BeltStatus status;
    unsigned int burgers_processed;
    unsigned int current_order_id; // ID de la orden que está procesando
    bool running; // Para indicar al proceso si debe terminar
} PreparationBelt;

// Estructura para la cola de órdenes en espera (implementación FIFO simple)
typedef struct {
    BurgerOrder orders[MAX_ORDERS_IN_QUEUE];
    int head; // Puntero al primer elemento
    int tail; // Puntero a la última posición libre
    int count; // Número de elementos en la cola
    pthread_mutex_t mutex; // Mutex para proteger el acceso a la cola
} OrderQueue;


// --- La Estructura Principal de Memoria Compartida ---
// Contiene todo el estado del sistema.
typedef struct {
    // --- Estado del Sistema ---
    bool system_running; // Para indicar a todos los procesos que terminen
    Ingredient ingredients[MAX_INGREDIENTS];
    PreparationBelt belts[MAX_BELTS];
    int num_belts; // Número de bandas activas, definido por línea de comando

    // --- Cola de Órdenes (Problema Productor-Consumidor) ---
    OrderQueue waiting_orders;

    // --- Primitivas de Sincronización ---
    // Semáforo que cuenta el número de órdenes en la cola `waiting_orders`.
    // Las bandas (consumidores) harán `sem_wait` aquí.
    sem_t sem_orders_available;

    // Semáforo que cuenta los espacios libres en la cola.
    // El generador (productor) hará `sem_wait` aquí para no desbordar la cola.
    sem_t sem_space_available;

} SharedSystemState;


// Nombre del objeto de memoria compartida. Será como un "archivo" en /dev/shm/
#define SHM_NAME "/burger_machine_shm"

#endif // SHARED_DATA_H