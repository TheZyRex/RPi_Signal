/**
 * @file main.c
 * @brief Main program for toggling GPIO pin and logging jitter measurements.
 */

#include "main.h"
#include "ringbuffer.h"

#define BUFFER_SIZE (10 * 1024 * sizeof(measurement_t))

/**
 * @brief Toggle the GPIO pin state.
 *
 * @param gpio The GPIO handle.
 * @param current_state The current state of the GPIO pin.
 * @return int The new state of the GPIO pin.
 */
int toggle_gpio(gpio_handle_t* gpio, int current_state) {
    current_state = !current_state;
    gpiod_line_set_value(gpio->line, current_state);
    return current_state;
}

/**
 * @brief Worker thread that toggles a GPIO pin and logs the delay into a ring buffer.
 *
 * This function runs in a separate thread and toggles a GPIO pin at a specified period.
 * It logs the time difference between toggles into a ring buffer.
 *
 * @param args Pointer to the thread arguments (thread_args_t).
 * @return void* Always returns NULL.
 */
void* worker_signal_gen(void* args) {
    thread_args_t* param = (thread_args_t*)args;
    
    /* Fixiate this thread to CPU_CORE */
    stick_thread_to_core(CPU_CORE);

    // Optionally set thread priority -- this requires ROOT privileges!
    // set_thread_priority(SCHED_PRIO);

    int current_state = 0;
    uint64_t expire;
    uint64_t sampleCount = 0;
    struct timespec start, now;

    /* Configure one-shot timer */
    struct itimerspec timerSpec;
    timerSpec.it_interval.tv_sec = 0;
    timerSpec.it_interval.tv_nsec = 0;
    timerSpec.it_value.tv_sec = 0;
    timerSpec.it_value.tv_nsec = PERIOD_NS;

    /* Start one-shot timer and begin time measurement */
    timerfd_settime(param->timer_fd, 0, &timerSpec, NULL);
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    /* Until user stops main program */
    while (!param->killswitch) {
        /* Read will block until timer has triggered */
        read(param->timer_fd, &expire, sizeof(expire));
        
        /* Get timestamp */
        clock_gettime(CLOCK_MONOTONIC_RAW, &now);

        /* Toggle GPIO pin */
        current_state = toggle_gpio(param->gpio, current_state);

        /* Reset timer after toggle */
        timerfd_settime(param->timer_fd, 0, &timerSpec, NULL);

        /* Calculate time difference */
        uint64_t diff = timespec_delta_nanoseconds(&now, &start);
        start = now;

        /* Write time difference to ring buffer */
        measurement_t m;
        m.sampleCount = sampleCount++;
        m.diff = diff;
        ring_buffer_queue_arr(param->rbuffer, (char*)&m, sizeof(measurement_t));
    }
    pthread_exit(NULL);
}

/**
 * @brief Main function to initialize GPIO, start worker threads, and handle user input.
 *
 * This function initializes the GPIO, sets up the ring buffer, and starts the worker threads
 * for signal generation and data handling. It waits for user input to stop the program and
 * performs cleanup before exiting.
 *
 */
int main() {
    gpio_handle_t *gpio = init_gpio(GPIO_PIN, GPIO_CHIP);
    if (!gpio) {
        fprintf(stderr, "Error GPIO init failed\n");
        return EXIT_FAILURE;
    }

    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (tfd == -1) {
        fprintf(stderr, "Error creating timer\n");
        gpiod_chip_close(gpio->chip);
        free(gpio);
        return EXIT_FAILURE;
    }

    /* Initialize the ring buffer for measurements */
    char buffer[BUFFER_SIZE];
    ring_buffer_t ring_buffer;
    ring_buffer_init(&ring_buffer, buffer, BUFFER_SIZE);

    thread_args_t targs;
    targs.gpio = gpio;
    targs.rbuffer = &ring_buffer;
    targs.killswitch = 0;
    targs.doPlot = 1;
    targs.timer_fd = tfd;

    pthread_t worker, plot_thread;
    int ret = pthread_create(&worker, NULL, &worker_signal_gen, &targs);
    if (ret != 0) {
        fprintf(stderr, "Error spawning Worker-Thread\n");
        return EXIT_FAILURE;
    }

    ret = pthread_create(&plot_thread, NULL, &worker_data_handler, &targs);
    if (ret != 0) {
        fprintf(stderr, "Error spawning Plot-Thread\n");
        return EXIT_FAILURE;
    }

    /* Wait for user input to stop the program */
    getchar();
    targs.killswitch = 1;

    pthread_join(worker, NULL);
    pthread_join(plot_thread, NULL);

    /* Clean up */
    close(tfd);
    gpiod_chip_close(gpio->chip);
    free(gpio);

    return EXIT_SUCCESS;
}