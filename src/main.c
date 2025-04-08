/**
 * 
 */

#include "../inc/main.h"
#include "../inc/ringbuffer.h"

/**
 * @brief Worker thread that toggles a GPIO pin and logs the delay into a ring buffer.
 *
 * This function runs in a separate thread and toggles a GPIO pin at a specified period.
 * It logs the time difference between toggles into a ring buffer.
 *
 * @param args Pointer to the thread arguments (thread_args_t).
 * @return void* Always returns NULL.
 */
void* func_signal_gen(void* args) {
    thread_args_t* param = (thread_args_t*)args;
    
    /* Fixiate this thread to CPU_CORE */
    stick_thread_to_core(param->core_id);

    /* Set thread priority - only if configured */
    if (param->sched_prio >= 1) {
        set_thread_priority(param->sched_prio);
    }

    /* calculate clock_gettime overhead */
    uint64_t err = get_clock_gettime_overhead();

    uint8_t current_state = 0;
    uint64_t expire;
    struct timespec start, now, target;
    //target.tv_sec = 0;
    //target.tv_nsec = param->period_ns;

    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    target = start;


    /* Until user stops main program */
    while (!param->killswitch) {
        /* configure absolut sleep time */
        target.tv_nsec += param->period_ns;
        while (target.tv_nsec >= 1e9) {
            target.tv_sec++;
            target.tv_nsec -= 1e9;
        }

        /* sleep for until specified time */
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, NULL);

        /* sleep for fixed amount of time */
        //clock_nanosleep(CLOCK_MONOTONIC, 0, &target, NULL);

        /* get timestamp after tigger */
        clock_gettime(CLOCK_MONOTONIC_RAW, &now);

        /* Toggle GPIO pin */
        current_state = !current_state;
        gpiod_line_set_value(param->gpio->line, current_state);

        /* calculate time difference, corrected by clock_gettime overhead */
        uint64_t diff = timespec_delta_nanoseconds(&now, &start) - err;

        /* Write time difference to ring buffer */
        ring_buffer_queue_arr(param->rbuffer, (char*)&diff, sizeof(uint64_t));

        /* Reset start time */
        start = now;
    }
    pthread_exit(NULL);
}

/**
 * @brief Main. 
 */
int main(int argc, char** argv) {

    thread_args_t targs;
    parse_user_args(argc, argv, &targs);

    /* initialize GPIO Port with default from config.h */
    if (targs.gpio == NULL) {
        targs.gpio = init_gpio(GPIO_PIN, GPIO_CHIP);
    }

    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (tfd < 0) {
        fprintf(stderr, "Error creating timerfd\n");
        gpiod_chip_close(targs.gpio->chip);
        free(targs.gpio);
        return EXIT_FAILURE;
    }

    /* Initialize ring buffer storing measurement results */
    size_t buffer_size = RING_BUFFER_SIZE * sizeof(measurement_t);
    char buffer[buffer_size];
    ring_buffer_t ring_buffer;
    ring_buffer_init(&ring_buffer, buffer, buffer_size);

    /* configure thread arguments */
    targs.rbuffer = &ring_buffer;
    targs.killswitch = 0;
    targs.timer_fd = tfd;

    /* Create and start worker threads */
    pthread_t worker_signal_gen, worker_data_handler;
    int ret = pthread_create(&worker_signal_gen, NULL, &func_signal_gen, &targs);
    if (ret != 0) {
        fprintf(stderr, "Error spawning Worker-Thread\n");
        return EXIT_FAILURE;
    }

    ret = pthread_create(&worker_data_handler, NULL, &func_data_handler, &targs);
    if (ret != 0) {
        fprintf(stderr, "Error spawning Plot-Thread\n");
        return EXIT_FAILURE;
    }

    /* Wait for user input to stop the program */
    printf("Press Enter to stop...\n");
    getchar();
    targs.killswitch = 1;

    pthread_join(worker_signal_gen, NULL);
    pthread_join(worker_data_handler, NULL);

    /* Clean up */
    gpiod_chip_close(targs.gpio->chip);
    free(targs.gpio);

    return EXIT_SUCCESS;
}