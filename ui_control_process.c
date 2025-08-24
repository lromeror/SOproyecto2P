// File: ui_control_process.c

#include <ncurses.h> // La biblioteca para la TUI
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h> // Para kill()
#include <string.h>   // <--- AÑADIR ESTA LÍNEA
#include <stdlib.h>   // <--- AÑADIR ESTA LÍNEA

#include "shared_data.h"

// --- Variables Globales (solo para este proceso) ---
static SharedSystemState *shared_state = NULL;

// --- Funciones de Dibujo ---

void draw_status_window(WINDOW *win) {
    wclear(win); // Limpia la ventana antes de redibujar
    box(win, 0, 0); // Dibuja un borde alrededor

    mvwprintw(win, 1, 2, "ESTADO DEL SISTEMA DE HAMBURGUESAS");
    
    // --- Estado de las Bandas ---
    mvwprintw(win, 3, 2, "Banda | PID     | Estado          | Hamburguesas Procesadas");
    mvwprintw(win, 4, 2, "------+---------+-----------------+--------------------------");
    for (int i = 0; i < shared_state->num_belts; i++) {
        char status_str[20];
        switch (shared_state->belts[i].status) {
            case IDLE: strcpy(status_str, "Esperando"); break;
            case PREPARING: strcpy(status_str, "Preparando"); break;
            case PAUSED: strcpy(status_str, "Pausada"); break;
            case NO_INGREDIENTS: strcpy(status_str, "Sin Ingredientes"); break;
            default: strcpy(status_str, "Desconocido"); break;
        }
        mvwprintw(win, 5 + i, 2, " %-4d | %-7d | %-15s | %u", 
                  i, 
                  shared_state->belts[i].pid, 
                  status_str, 
                  shared_state->belts[i].burgers_processed);
    }

    // --- Estado de la Cola de Órdenes ---
    int queue_y_pos = 5 + shared_state->num_belts + 2;
    mvwprintw(win, queue_y_pos, 2, "COLA DE ÓRDENES: %d/%d", shared_state->waiting_orders.count, MAX_ORDERS_IN_QUEUE);

    // --- Inventario ---
    int inv_y_pos = queue_y_pos + 2;
    mvwprintw(win, inv_y_pos, 2, "INVENTARIO DE INGREDIENTES:");
    for (int i = 0; i < MAX_INGREDIENTS; i++) {
        if (strlen(shared_state->ingredients[i].name) > 0) {
            mvwprintw(win, inv_y_pos + 1 + i, 4, "- %-10s: %d", shared_state->ingredients[i].name, shared_state->ingredients[i].count);
        }
    }

    wrefresh(win); // Actualiza la pantalla con lo que dibujamos
}

void draw_control_window(WINDOW *win) {
    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "Controles: (P)ausar Banda | (R)eanudar Banda | (Ctrl+C para Salir)");
    wrefresh(win);
}

// --- Función Principal de la UI y Control ---
void start_ui_control_process(const char* shm_name) {
    // --- 1. Conexión a Memoria Compartida ---
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) { /* ... manejo de error ... */ exit(1); }
    shared_state = mmap(NULL, sizeof(SharedSystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_state == MAP_FAILED) { /* ... manejo de error ... */ exit(1); }
    close(shm_fd);

    // --- 2. Inicialización de ncurses ---
    initscr();             // Iniciar modo ncurses
    cbreak();              // Deshabilitar buffer de línea
    noecho();              // No mostrar las teclas presionadas
    curs_set(0);           // Ocultar el cursor
    timeout(100);          // Hacer que getch() no sea bloqueante (espera 100ms)

    // Crear las ventanas
    int height, width;
    getmaxyx(stdscr, height, width);
    WINDOW *status_win = newwin(height - 3, width, 0, 0);
    WINDOW *control_win = newwin(3, width, height - 3, 0);

    // --- 3. Bucle Principal ---
    while (shared_state->system_running) {
        draw_status_window(status_win);
        draw_control_window(control_win);

        // --- 4. Manejo de Entrada ---
        int ch = getch(); // Lee una tecla (o devuelve ERR si pasa el timeout)

        if (ch == 'p' || ch == 'P' || ch == 'r' || ch == 'R') {
            echo(); // Activar echo para que el usuario vea lo que escribe
            mvwprintw(control_win, 1, 55, "ID de la banda?: ");
            wrefresh(control_win);
            
            char str[4];
            wgetnstr(control_win, str, 3); // Leer hasta 3 caracteres
            int belt_id_input = atoi(str);

            noecho(); // Desactivar echo de nuevo

            // Validar entrada
            if (belt_id_input >= 0 && belt_id_input < shared_state->num_belts) {
                pid_t target_pid = shared_state->belts[belt_id_input].pid;
                if (ch == 'p' || ch == 'P') {
                    // **Concepto S.O. (Silberschatz): Manejo de Procesos y Señales**
                    // Enviamos la señal SIGSTOP para pausar el proceso incondicionalmente.
                    if (kill(target_pid, SIGSTOP) == 0) {
                        shared_state->belts[belt_id_input].status = PAUSED;
                    }
                } else if (ch == 'r' || ch == 'R') {
                    // Enviamos la señal SIGCONT para reanudar un proceso pausado.
                    if (kill(target_pid, SIGCONT) == 0) {
                        shared_state->belts[belt_id_input].status = IDLE; // Volver a estado idle
                    }
                }
            }
        }
    }

    // --- 5. Limpieza ---
    delwin(status_win);
    delwin(control_win);
    endwin(); // Restaurar la terminal a su estado original

    printf("[UI/Control] Terminando...\n");
    munmap(shared_state, sizeof(SharedSystemState));
}