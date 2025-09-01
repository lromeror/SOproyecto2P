# SOproyecto2P

# Simulación de Hamburguesería con Procesos

## Descripción del Proyecto

Este proyecto es una simulación de una línea de producción de hamburguesas, desarrollado en lenguaje C para sistemas operativos tipo Unix (Linux). El objetivo es demostrar el uso de concurrencia y comunicación entre procesos (`fork`, `wait`), memoria compartida (`shm`, `mmap`) y primitivas de sincronización (`mutex`, `semáforos`).

El sistema consta de varios procesos independientes que colaboran:

*   Un **Generador de Órdenes** que crea pedidos de hamburguesas y los añade a una cola FIFO.
*   Múltiples **Bandas de Preparación**, cada una ejecutándose en su propio proceso, que toman órdenes de la cola y las procesan.
*   Una **Interfaz de Usuario** en la terminal que muestra el estado del sistema en tiempo real (inventario, estado de las bandas, órdenes en cola) y permite la interacción del usuario.

## Características Principales

*   **Comunicación entre Procesos (IPC):** Todo el estado del sistema se comparte a través de un único segmento de memoria compartida.
*   **Sincronización:** Se utilizan semáforos para gestionar el flujo de producción (productor-consumidor) y mutex para proteger el acceso a datos críticos como el inventario y la cola de órdenes.
*   **Interfaz Interactiva (TUI):** Construida con la librería `ncurses` para ofrecer una visualización dinámica y controles para pausar/reanudar bandas o reponer ingredientes.
*   **Lógica de Producción:** El sistema se detiene automáticamente si faltan ingredientes para una orden y se reanuda cuando el usuario los repone a través de la interfaz.

## Requisitos y Dependencias

Antes de compilar y ejecutar, asegúrate de tener instalado un compilador de C (`gcc`) y las siguientes librerías de desarrollo.

### Librería Necesaria

El proyecto utiliza `ncurses` para la interfaz de usuario en la terminal.

*   **En sistemas Debian/Ubuntu:**
    ```bash
    sudo apt-get update
    sudo apt-get install build-essential libncurses5-dev
    ```

*   **En sistemas Fedora/CentOS/RHEL:**
    ```bash
    sudo dnf install ncurses-devel
    ```

*   **En sistemas Arch Linux:**
    ```bash
    sudo pacman -S ncurses
    ```

## Compilación y Ejecución

El proyecto incluye un `Makefile` que simplifica la compilación.

1.  **Compilar el proyecto:**
    ```bash
    make
    ```

2.  **Ejecutar el programa** (con 5 bandas por defecto):
    ```bash
    ./burger_machine
    ```
    O especificar un número diferente de bandas (ej. 3):
    ```bash
    ./burger_machine 3
    ```
3.  **Limpiar archivos compilados:**
    ```bash
    make clean
    ```