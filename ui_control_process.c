// File: ui_control_process.c (Versión con Reposición de Ingredientes y Despacho)

#include <ncurses.h> 
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h> 
#include <string.h>   
#include <stdlib.h>   

#include "shared_data.h"

static SharedSystemState *shared_state = NULL;
volatile sig_atomic_t ui_should_exit = 0;

void ui_signal_handler(int sig) {
    if (sig == SIGINT) {
        ui_should_exit = 1;
    }
}

void draw_status_window(WINDOW *win) {
    wclear(win);
    box(win, 0, 0); 
    mvwprintw(win, 1, 2, "ESTADO DEL SISTEMA DE HAMBURGUESAS");
    
    mvwprintw(win, 3, 2, "Banda | PID     | Estado          | Hamburguesas Procesadas");
    mvwprintw(win, 4, 2, "------+---------+-----------------+--------------------------");
    for (int i = 0; i < shared_state->num_belts; i++) {
        char status_str[25];
        switch (shared_state->belts[i].status) {
            case IDLE: strcpy(status_str, "Esperando"); break;
            case PREPARING: snprintf(status_str, 25, "Preparando #%u", shared_state->belts[i].current_order_id); break;
            case PAUSED: strcpy(status_str, "Pausada"); break;
            case NO_INGREDIENTS: strcpy(status_str, "**FALTAN ING.**"); break;
            default: strcpy(status_str, "Desconocido"); break;
        }
        mvwprintw(win, 5 + i, 2, " %-4d | %-7d | %-15s | %u", i, shared_state->belts[i].pid, status_str, shared_state->belts[i].burgers_processed);
    }

    int queue_y_pos = 5 + shared_state->num_belts + 2;
    mvwprintw(win, queue_y_pos, 2, "COLA DE ORDENES EN ESPERA: %d/%d", shared_state->waiting_orders.count, MAX_ORDERS_IN_QUEUE);

    int inv_y_pos = queue_y_pos + 2;
    mvwprintw(win, inv_y_pos, 2, "INVENTARIO DE INGREDIENTES:");
    for (int i = 0; i < 6; i++) { // Asumimos 6 ingredientes
        if (strlen(shared_state->ingredients[i].name) > 0) {
             mvwprintw(win, inv_y_pos + 1 + i, 4, "- %-10s: %d", shared_state->ingredients[i].name, shared_state->ingredients[i].count);
        }
    }

    int alert_y_pos = inv_y_pos + 8;
    mvwprintw(win, alert_y_pos, 2, "ALERTAS DEL SISTEMA:");
    int alert_count = 0;
    for (int i = 0; i < shared_state->num_belts; i++) {
        if (shared_state->belts[i].status == NO_INGREDIENTS) {
            wattron(win, A_BOLD);
            mvwprintw(win, alert_y_pos + 1 + alert_count, 4, "-> Banda %d parada por falta de ingredientes para orden #%u", i, shared_state->belts[i].current_order_id);
            wattroff(win, A_BOLD);
            alert_count++;
        }
    }
    if (alert_count == 0) {
        mvwprintw(win, alert_y_pos + 1, 4, "- Todo en orden.");
    }
    
    wrefresh(win);
}

void draw_control_window(WINDOW *win) {
    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "Controles: (P)ausar | (R)eanudar | (A)ñadir Ingredientes | (Ctrl+C Salir)");
    wrefresh(win);
}

void start_ui_control_process(const char* shm_name) {
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) { exit(1); }
    shared_state = mmap(NULL, sizeof(SharedSystemState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_state == MAP_FAILED) { exit(1); }
    close(shm_fd);

    signal(SIGINT, ui_signal_handler);

    initscr(); cbreak(); noecho(); curs_set(0); timeout(100);

    int height, width;
    getmaxyx(stdscr, height, width);
    WINDOW *status_win = newwin(height - 4, width, 0, 0);
    WINDOW *control_win = newwin(4, width, height - 4, 0);

    while (shared_state->system_running && !ui_should_exit) {
        draw_status_window(status_win);
        draw_control_window(control_win);
        int ch = getch(); 

        if (ch == 'p' || ch == 'P' || ch == 'r' || ch == 'R') {
            echo(); 
            wmove(control_win, 2, 2); wclrtoeol(control_win);
            mvwprintw(control_win, 2, 2, "ID de la banda?: ");
            wrefresh(control_win);
            char str[4]; wgetnstr(control_win, str, 3); int belt_id_input = atoi(str);
            noecho(); 

            if (belt_id_input >= 0 && belt_id_input < shared_state->num_belts) {
                pid_t target_pid = shared_state->belts[belt_id_input].pid;
                if (ch == 'p' || ch == 'P') {
                    if (kill(target_pid, SIGSTOP) == 0) { shared_state->belts[belt_id_input].status = PAUSED; }
                } else if (ch == 'r' || ch == 'R') {
                    if (kill(target_pid, SIGCONT) == 0) { shared_state->belts[belt_id_input].status = IDLE; }
                }
            }
        }

        // --- LÓGICA PARA REPONER INGREDIENTES ---
        if (ch == 'a' || ch == 'A') {
            echo();
            
            wmove(control_win, 2, 2); wclrtoeol(control_win);
            mvwprintw(control_win, 2, 2, "Añadir -> ID (0:Pan 1:Carne 2:Lec 3:Tom 4:Ceb 5:Que): ");
            wrefresh(control_win);
            char str[4]; wgetnstr(control_win, str, 3); int ing_id = atoi(str);

            if (ing_id >= 0 && ing_id < 6) {
                wmove(control_win, 2, 2); wclrtoeol(control_win);
                mvwprintw(control_win, 2, 2, "Cantidad a añadir para %s: ", shared_state->ingredients[ing_id].name);
                wrefresh(control_win);
                wgetnstr(control_win, str, 3); int quantity = atoi(str);

                if (quantity > 0) {
                    pthread_mutex_lock(&shared_state->ingredients[ing_id].mutex);
                    shared_state->ingredients[ing_id].count += quantity;
                    pthread_mutex_unlock(&shared_state->ingredients[ing_id].mutex);

                    // --- DESPACHO AUTOMÁTICO ---
                    // Despertamos a las bandas. Al haber nuevos ingredientes, la que estaba
                    // atascada ahora podrá continuar su trabajo.
                    for(int i=0; i < shared_state->num_belts; i++) {
                       sem_post(&shared_state->sem_orders_available);
                    }
                }
            }
            noecho();
        }
    }

    delwin(status_win); delwin(control_win); endwin(); 
    munmap(shared_state, sizeof(SharedSystemState));
}