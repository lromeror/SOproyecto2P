// File: ui_control_process.c

#include <ncurses.h> 
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h> 
#include <string.h>   
#include <stdlib.h>   

#include "shared_data.h"

// puntero a la memoria compartida
static SharedSystemState *shared_state = NULL;


// funcion para dibujar la ventana principal con toda la informacion
void draw_status_window(WINDOW *win) {
    wclear(win); // limpiamos la ventana antes de dibujar
    box(win, 0, 0); // le ponemos un borde

    mvwprintw(win, 1, 2, "ESTADO DEL SISTEMA DE HAMBURGUESAS");
    
    mvwprintw(win, 3, 2, "Banda | PID     | Estado          | Hamburguesas Procesadas");
    mvwprintw(win, 4, 2, "------+---------+-----------------+--------------------------");
    for (int i = 0; i < shared_state->num_belts; i++) {
        char status_str[20];
        // convertimos el enum de estado a un string para mostrarlo
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

    // --- estado de la cola de ordenes ---
    int queue_y_pos = 5 + shared_state->num_belts + 2;
    mvwprintw(win, queue_y_pos, 2, "COLA DE ORDENES: %d/%d", shared_state->waiting_orders.count, MAX_ORDERS_IN_QUEUE);

    // --- inventario ---
    int inv_y_pos = queue_y_pos + 2;
    mvwprintw(win, inv_y_pos, 2, "INVENTARIO DE INGREDIENTES:");
    for (int i = 0; i < MAX_INGREDIENTS; i++) {
        if (strlen(shared_state->ingredients[i].name) > 0) {
            mvwprintw(win, inv_y_pos + 1 + i, 4, "- %-10s: %d", shared_state->ingredients[i].name, shared_state->ingredients[i].count);
        }
    }

    wrefresh(win); // actualiza la pantalla con todo lo que dibujamos
}

// funcion para dibujar la ventana de abajo con los controles
void draw_control_window(WINDOW *win) {
    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "Controles: (P)ausar Banda | (R)eanudar Banda | (Ctrl+C para Salir)");
    wrefresh(win);
}


void start_ui_control_process(const char* shm_name) {

    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {  exit(1); }
    shared_state = mmap(NULL, sizeof(SharedSystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_state == MAP_FAILED) { /* ... manejo de error ... */ exit(1); }
    close(shm_fd);


    initscr();             // iniciar modo ncurses
    cbreak();              // deshabilitar buffer de linea, para leer tecla por tecla
    noecho();              // no mostrar las teclas que el usuario presiona
    curs_set(0);           // ocultar el cursor
    timeout(100);         

    // creamos las dos ventanas que vamos a usar
    int height, width;
    getmaxyx(stdscr, height, width);
    WINDOW *status_win = newwin(height - 3, width, 0, 0);
    WINDOW *control_win = newwin(3, width, height - 3, 0);

    // bucle principal de la interfaz
    while (shared_state->system_running) {
        // redibujamos las ventanas en cada ciclo para que la info este fresca
        draw_status_window(status_win);
        draw_control_window(control_win);

        // esperamos por una tecla del usuario
        int ch = getch(); 

        // logica para pausar o reanudar una banda
        if (ch == 'p' || ch == 'P' || ch == 'r' || ch == 'R') {
            echo(); // activamos el echo temporalmente para ver lo que escribimos
            mvwprintw(control_win, 1, 55, "ID de la banda?: ");
            wrefresh(control_win);
            
            char str[4];
            wgetnstr(control_win, str, 3); // leemos el numero que introduce el usuario
            int belt_id_input = atoi(str);

            noecho(); // desactivamos el echo de nuevo

            // verificamos que el id de la banda sea valido
            if (belt_id_input >= 0 && belt_id_input < shared_state->num_belts) {
                pid_t target_pid = shared_state->belts[belt_id_input].pid;
                if (ch == 'p' || ch == 'P') {
                    // enviamos la senal sigstop para pausar el proceso de la banda
                    if (kill(target_pid, SIGSTOP) == 0) {
                        // si la senal se envio bien, actualizamos el estado para que se refleje
                        shared_state->belts[belt_id_input].status = PAUSED;
                    }
                } else if (ch == 'r' || ch == 'R') {
                    // enviamos la senal sigcont para reanudar un proceso pausado
                    if (kill(target_pid, SIGCONT) == 0) {
                        shared_state->belts[belt_id_input].status = IDLE; // la ponemos en idle
                    }
                }
            }
        }
    }

    delwin(status_win);
    delwin(control_win);
    endwin(); 

    printf("[UI/Control] Terminando...\n");
    munmap(shared_state, sizeof(SharedSystemState));
}