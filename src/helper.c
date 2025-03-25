#include "main.h"

#define INITIAL_CAPACITY 1024
#define CAPACITY_MULTIPLIER 2

/**
 * @brief Initialize the GPIO port and return a handle.
 *
 * @param gpio_pin The GPIO pin number.
 * @param gpio_chip The GPIO chip name.
 * @return gpio_handle_t* Pointer to the GPIO handle, or NULL on failure.
 */
gpio_handle_t* init_gpio(int gpio_pin, const char* gpio_chip) {
    gpio_handle_t* handle = malloc(sizeof(gpio_handle_t));
    if (!handle) {
        perror("Fehler bei malloc");
        return NULL;
    }

    if (gpio_chip == NULL) {
        perror("Fehler, kein GPIO Chip gegeben. Nutze gpioinfo, um herauszufinden welchen chip du benötigst");
        free(handle);
        return NULL;
    }
    handle->chip = gpiod_chip_open(gpio_chip);
    if (!handle->chip) {
        perror("Fehler beim Öffnen des GPIO-Chips");
        free(handle);
        return NULL;
    }
    handle->line = gpiod_chip_get_line(handle->chip, gpio_pin);
    if (!handle->line) {
        perror("Fehler beim Abrufen der GPIO-Leitung");
        gpiod_chip_close(handle->chip);
        free(handle);
        return NULL;
    }
    if (gpiod_line_request_output(handle->line, "RPiSignal", 0) < 0) {
        perror("Fehler bei der Konfiguration der GPIO-Leitung als Ausgang");
        gpiod_chip_close(handle->chip);
        free(handle);
        return NULL;
    }
    return handle;
}

/**
 * @brief Bind the thread to a specific CPU core.
 *
 * @param core_id The CPU core ID.
 * @return int 0 on success, or an error code on failure.
 */
int stick_thread_to_core(uint8_t core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if(ret != 0) {
        perror("Fehler beim Setzen der CPU-Affinität\n");
    }
    return ret;
}

/**
 * @brief Set thread priority (if needed).
 *
 * @param priority The thread priority.
 * @return int 0 on success, or an error code on failure.
 */
int set_thread_priority(uint8_t priority) {
    struct sched_param schedParam;
    schedParam.sched_priority = priority;
    int ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &schedParam);
    if(ret != 0) {
        perror("Fehler beim Setzen des Echtzeit-Schedulings");
    }
    return ret;
}

/**
 * @brief Calculate the difference in nanoseconds between two timespecs.
 *
 * @param end The end timespec.
 * @param start The start timespec.
 * @return uint64_t The difference in nanoseconds.
 */
inline uint64_t timespec_delta_nanoseconds(struct timespec* end, struct timespec* start) {
    return (((end->tv_sec - start->tv_sec) * 1.0e9) + (end->tv_nsec - start->tv_nsec));
}

/**
 * @brief Dequeue measurements from the ring buffer and store them in a dynamically allocated array.
 *
 * @param rbuffer The ring buffer.
 * @param all_measurements Pointer to the array of measurements.
 * @param all_count Pointer to the count of measurements.
 * @param capacity Pointer to the capacity of the array.
 * @return int 0 on success, or -1 on failure.
 */
int dequeue_measurements(ring_buffer_t* rbuffer, measurement_t** all_measurements, size_t* all_count, size_t* capacity) {
    measurement_t m;
    while (ring_buffer_dequeue_arr(rbuffer, (char*)&m, sizeof(measurement_t)) == sizeof(measurement_t)) {
        if (*all_count >= *capacity) {
            size_t new_capacity = (*capacity == 0) ? INITIAL_CAPACITY : (*capacity * CAPACITY_MULTIPLIER);
            measurement_t* temp = realloc(*all_measurements, new_capacity * sizeof(measurement_t));
            if (!temp) {
                perror("realloc failed");
                return -1;
            }
            *all_measurements = temp;
            *capacity = new_capacity;
        }
        (*all_measurements)[(*all_count)++] = m;
    }
    return 0;
}

/**
 * @brief Write measurements to a CSV file.
 *
 * @param filename The name of the CSV file.
 * @param m The array of measurements.
 * @param num The number of measurements.
 */
void write_to_file(const char* filename, measurement_t* m, size_t num) {
    if (filename == NULL || m == NULL) {
        return;
    }

    FILE* fp = fopen(filename, "a");
    if (fp == NULL) {
        return;
    }

    for (size_t i = 0; i < num; ++i) {
        fprintf(fp, "%ld,%ld\n", m[i].sampleCount, m[i].diff);
    }

    fclose(fp);
}

/**
 * @brief Worker thread for handling data and plotting jitter using GNUPlot.
 *
 * This function runs in a separate thread and handles the data processing and plotting
 * of jitter between consecutive GPIO pin toggles. It dequeues measurements from a ring buffer,
 * stores them in a dynamically allocated array, and optionally plots the jitter using GNUPlot.
 * The function also writes all recorded timestamps to a CSV file upon termination.
 *
 * @param args Pointer to the thread arguments (thread_args_t).
 * @return void* Always returns NULL.
 */
void* worker_data_handler(void* args) {
    thread_args_t* param = (thread_args_t*)args;
    FILE* gp = NULL;

    char filename[64];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    // Format: "data_20230324_134501.csv"
    strftime(filename, sizeof(filename), "jitter_log_%Y%m%d_%H%M%S.csv", tm_info);
    
    if (param->doPlot) {
        gp = setup_gnuplot();
    }

    measurement_t* all_measurements = NULL;
    size_t all_count = 0, capacity = 0;

    while (!param->killswitch) {
        if (dequeue_measurements(param->rbuffer, &all_measurements, &all_count, &capacity) != 0) {
            break;
        }

        if (param->doPlot) {
            plot_to_gnuplot(all_measurements, all_count, gp);
        }

        usleep(WINDOW_REFRESH * 1000);  // WINDOW_REFRESH in ms (e.g. 500 ms)
    }
        
    /* Write all recorded timestamps to csv file */
    write_to_file(filename, all_measurements, all_count);

    free(all_measurements);
    if (gp) {
        fclose(gp);
    }

    pthread_exit(NULL);
}

/**
 * @brief Setup GNUPlot for plotting.
 *
 * @return FILE* Pointer to the GNUPlot pipe, or NULL on failure.
 */
FILE* setup_gnuplot() {
    FILE *gp = popen("gnuplot -persistent", "w");
    if (!gp) {
        perror("Could not open gnuplot pipe");
        return NULL;
    }

    /* Gnuplot Setup */
    fprintf(gp, "set terminal qt size 1200,700\n");
    fprintf(gp, "set title 'GPIO Toggle Jitter'\n");
    fprintf(gp, "set xlabel 'Sample Count'\n");
    fprintf(gp, "set ylabel 'Jitter (ns)'\n");
    fprintf(gp, "set key outside\n");
    fprintf(gp, "set ytics auto\n");
    fprintf(gp, "set format y '%%.0f'\n");
    fflush(gp);

    return gp;
}

/**
 * @brief Plot jitter data to GNUPlot.
 *
 * @param m The array of measurements.
 * @param num The number of measurements.
 * @param gp The GNUPlot pipe.
 */
void plot_to_gnuplot(measurement_t* m, size_t num, FILE* gp) {
    if (num > 0) {
        size_t start_index = (num > WINDOW_SIZE) ? (num - WINDOW_SIZE) : 0;
        size_t count = num - start_index;
        int64_t jitters[WINDOW_SIZE] = {0};

        for (size_t i = start_index; i < num; i++) {
            int64_t diff = (int64_t)m[i].diff;
            int64_t jitter = diff - (int64_t)PERIOD_NS;
            jitters[i - start_index] = jitter;
        }

        /* Compute jitter values and determine max and avg in one loop */
        int64_t max_abs = 0;
        int64_t min_abs = 0;
        int64_t sum_abs = 0;
        for (size_t i = 0; i < count; i++) {
            int64_t abs_val = (jitters[i] < 0) ? -jitters[i] : jitters[i];
            if (abs_val > max_abs)
                max_abs = abs_val;

            sum_abs += abs_val;
        }
        int64_t avg_abs = (count > 0) ? (sum_abs / count) : 0;

        /* Add margin (10% of range or at least 1000 ns) */
        int64_t margin = (max_abs < 1000) ? (max_abs / 10) : (max_abs / 10);
        if (margin == 0)
            margin = 1000;
        int64_t yr_min = -(max_abs * 0.10 + margin);
        int64_t yr_max = max_abs + margin;

        /* Determine x-axis range based on sampleCount */
        uint64_t x_min = m[start_index].sampleCount;
        uint64_t x_max = m[num - 1].sampleCount;
        fprintf(gp, "set xrange [%" PRIu64 ":%" PRIu64 "]\n", x_min, x_max);
        fprintf(gp, "set yrange [%" PRId64 ":%" PRId64 "]\n", yr_min, yr_max);

        /* Add dynamic Labels for MAX and AVG Jitter  */
        fprintf(gp, "unset label 2\n");
        fprintf(gp, "unset label 3\n");
        fprintf(gp, "unset label 4\n");
        fprintf(gp, "set label 2 'Max: %" PRId64 " ns' at screen 0.90, screen 0.55\n", max_abs);
        fprintf(gp, "set label 4 'Avg: %" PRId64 " ns' at screen 0.90, screen 0.45\n", avg_abs);

        /* Plot jitter data with reference */
        fprintf(gp, "plot '-' using 1:2 with linespoints pt 1 title 'Jitter (ns)', '-' using 1:2 with lines title 'Erwartet (0 ns)'\n");
        for (size_t i = start_index; i < num; i++) {
            uint64_t sample = m[i].sampleCount;
            fprintf(gp, "%" PRIu64 " %" PRId64 "\n", sample, jitters[i - start_index]);
        }
        fprintf(gp, "e\n");
        fprintf(gp, "%" PRIu64 " %d\n", x_min, 0);
        fprintf(gp, "%" PRIu64 " %d\n", x_max, 0);
        fprintf(gp, "e\n");
        fflush(gp);
    }
}