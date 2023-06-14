#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libcoro.h"

/**
 * You can compile and run this code using the commands:
 *
 * $> gcc solution.c libcoro.c
 * $> ./a.out
 */

struct single_sorted_file_data {
    int number_count;
    int *sorted_numbers;
};

void coro_yield_with_respect_to_quantum(struct timespec *coro_start, int quantum_soft_limit) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long time_spent_this_run = now.tv_nsec - coro_start->tv_nsec;
    if (time_spent_this_run >= quantum_soft_limit) {
        coro_yield();
        clock_gettime(CLOCK_MONOTONIC, coro_start);
    }
}

int quick_sort_partition(int number_count, int *numbers, struct timespec *coro_start, int quantum_soft_limit) {
    int pivot_value = numbers[number_count - 1];
    int pivot_index = 0;
    for (int i = 0; i < number_count; ++i) {
        if (numbers[i] <= pivot_value) {
            int temp = numbers[i];
            numbers[i] = numbers[pivot_index];
            numbers[pivot_index] = temp;
            pivot_index++;
        }
        coro_yield_with_respect_to_quantum(coro_start, quantum_soft_limit);
    }
    return pivot_index - 1;

}

int *get_sorted_inplace_numbers(int number_count, int *numbers, struct timespec *coro_start, int quantum_soft_limit) {
    if (number_count <= 1)
        return numbers;
    int pivot_index = quick_sort_partition(number_count, numbers, coro_start, quantum_soft_limit);
    get_sorted_inplace_numbers(pivot_index, numbers, coro_start, quantum_soft_limit);
    get_sorted_inplace_numbers(number_count - pivot_index, numbers + pivot_index, coro_start, quantum_soft_limit);

    return numbers;
}

struct single_sorted_file_data *
get_sorted_inplace_file_data(int number_count, int *numbers, struct timespec *coro_start, int quantum_soft_limit) {
    struct single_sorted_file_data *data = malloc(sizeof(struct single_sorted_file_data));

    data->number_count = number_count;
    data->sorted_numbers = get_sorted_inplace_numbers(number_count, numbers, coro_start, quantum_soft_limit);

    return data;
}

void dispose_of_sorted_file_data(struct single_sorted_file_data *data) {
    free(data->sorted_numbers);
    free(data);
}

struct file_queue {
    int file_ptr;
    int file_count;
    char **file_names;
    struct single_sorted_file_data **sorted_files;
};


struct file_queue *create_file_queue(int file_count, char **file_names) {
    struct file_queue *queue = malloc(sizeof(struct file_queue));

    queue->file_count = file_count;
    queue->file_ptr = 0;
    queue->file_names = malloc(sizeof(char *) * file_count);
    queue->sorted_files = malloc(sizeof(struct single_sorted_file_data *) * file_count);
    for (int i = 0; i < file_count; ++i) {
        queue->file_names[i] = file_names[i];
        queue->sorted_files[i] = NULL;
    }

    return queue;
}

void dispose_of_file_queue(struct file_queue *queue) {
    // We do not dispose of individual file names as they come from argv and are deallocated for us
    free(queue->file_names);
    for (int file_ptr = 0; file_ptr < queue->file_count; ++file_ptr) {
        dispose_of_sorted_file_data(queue->sorted_files[file_ptr]);
    }
    free(queue->sorted_files);
    free(queue);
}


struct coroutine_data {
    int quantum_soft_limit;
    char *name;
    struct file_queue *shared_file_queue;
    struct timespec start_timespec;
};


struct coroutine_data *create_coroutine_data(char *name, int quantum_soft_limit, struct file_queue *shared_file_queue) {
    struct coroutine_data *data = malloc(sizeof(struct coroutine_data));

    data->name = name;
    data->quantum_soft_limit = quantum_soft_limit;
    data->shared_file_queue = shared_file_queue;

    return data;
}

void dispose_of_coroutine_data(struct coroutine_data *data) {
    free(data->name);
    free(data);
}


int count_numbers_in_file(char *file_name) {
    FILE *fp = fopen(file_name, "r");
    int number_count = 0;

    int dummy_number;

    while (fscanf(fp, "%d", &dummy_number) == 1) {
        number_count++;
    }

    // after being done with fp
    fclose(fp);
    return number_count;
}

int *get_numbers_in_file(char *file_name, int number_count) {
    FILE *fp = fopen(file_name, "r");
    int number_ptr = 0;
    int *array = malloc(sizeof(int) * number_count);

    while (fscanf(fp, "%d", &array[number_ptr++]) == 1);

    // after being done with fp
    fclose(fp);
    return array;
}

/**
 * Coroutine body. This code is executed by all the coroutines. Here you
 * implement your solution, sort each individual file.
 */
static int
coroutine_func_f(void *coroutine_data) {
    struct coro *this = coro_this();
    struct coroutine_data *data = coroutine_data;
    printf("coroutine %s starts\n", data->name);
    clock_gettime(CLOCK_MONOTONIC, &data->start_timespec);

    while (data->shared_file_queue->file_ptr < data->shared_file_queue->file_count) {
        int file_ptr = data->shared_file_queue->file_ptr;
        data->shared_file_queue->file_ptr++;

        char *file_name = data->shared_file_queue->file_names[file_ptr];
        int number_count = count_numbers_in_file(file_name);
        int *numbers = get_numbers_in_file(file_name, number_count);

        printf("coroutine %s starts sorting file %s\n", data->name, file_name);
        data->shared_file_queue->sorted_files[file_ptr] = get_sorted_inplace_file_data(number_count, numbers,
                                                                                       &data->start_timespec,
                                                                                       data->quantum_soft_limit);
        printf("coroutine %s finishes sorting file %s\n", data->name, file_name);
    }
    printf("coroutine %s finished execution\n", data->name);

    dispose_of_coroutine_data(coroutine_data);
    return 0;
}

void output_merged_sorted_numbers_to_file(const struct file_queue *shared_file_queue, FILE *fp) {
    printf("started merging\n");
    int *file_ptr_to_current_number_ptr = malloc(sizeof(int) * shared_file_queue->file_count);
    for (int file_ptr = 0; file_ptr < shared_file_queue->file_count; ++file_ptr)
        file_ptr_to_current_number_ptr[file_ptr] = 0;
    while (true) {
        int *minimum_number = NULL;
        int minimum_number_file_ptr = -1;
        for (int file_ptr = 0; file_ptr < shared_file_queue->file_count; ++file_ptr) {
            int current_number_ptr = file_ptr_to_current_number_ptr[file_ptr];
            struct single_sorted_file_data *sorted_file = shared_file_queue->sorted_files[file_ptr];
            int *current_number = &sorted_file->sorted_numbers[current_number_ptr];
            if (sorted_file->number_count == current_number_ptr)
                continue;
            if (minimum_number == NULL || *minimum_number > *current_number) {
                minimum_number_file_ptr = file_ptr;
                minimum_number = current_number;
            }
        }
        if (minimum_number == NULL)
            break;
        file_ptr_to_current_number_ptr[minimum_number_file_ptr]++;
        fprintf(fp, "%d ", *minimum_number);
    }
    free(file_ptr_to_current_number_ptr);
    printf("finished merging\n");
}

int
main(int argc, char **argv) {
    const int non_file_name_cli_arguments_count = 3;

    /* argv[0] is the executable */
    int target_latency;
    sscanf(argv[1], "%i", &target_latency);
    int coroutine_count;
    sscanf(argv[2], "%i", &coroutine_count);
    int quantum = target_latency / coroutine_count;

    struct file_queue *shared_file_queue = create_file_queue(
            argc - non_file_name_cli_arguments_count,
            argv + non_file_name_cli_arguments_count
    );


    /* Initialize our coroutine global cooperative scheduler. */
    coro_sched_init();
    /* Start several coroutines. */
    for (int i = 0; i < coroutine_count; ++i) {
        char name[16];
        sprintf(name, "coro_%d", i);
        struct coroutine_data *coroutine_data = create_coroutine_data(strdup(name), quantum, shared_file_queue);
        coro_new(coroutine_func_f, coroutine_data);
    }
    /* Wait for all the coroutines to end. */
    struct coro *c;
    while ((c = coro_sched_wait()) != NULL) {
        /*
         * Each 'wait' returns a finished coroutine with which you can
         * do anything you want. Like check its exit status, for
         * example. Don't forget to free the coroutine afterwards.
         */
        printf("Finished %d\n", coro_status(c));
        coro_delete(c);
    }
    /* All coroutines have finished. */

    FILE *fp = fopen("tests_merged.txt", "w");
    output_merged_sorted_numbers_to_file(shared_file_queue, fp);
    fclose(fp);

    dispose_of_file_queue(shared_file_queue);

    return 0;
}

