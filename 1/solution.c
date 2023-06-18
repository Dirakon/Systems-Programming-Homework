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

const long nsec_in_sec = 1000000000;

const long nsec_in_milliseconds = 1000000;
const long milliseconds_in_sec = 1000;

const long nsec_in_microseconds = 1000;
const long microseconds_in_sec = 1000000;

/*
 * Based on https://stackoverflow.com/a/68804612
 */
struct timespec diff_timespec(const struct timespec spec1,
                              const struct timespec spec2) {
    struct timespec diff = {.tv_sec = spec1.tv_sec - spec2.tv_sec,
            .tv_nsec = spec1.tv_nsec - spec2.tv_nsec};
    if (diff.tv_nsec < 0) {
        diff.tv_nsec += nsec_in_sec;
        diff.tv_sec--;
    }
    return diff;
}

struct timespec add_timespec(const struct timespec spec1,
                             const struct timespec spec2) {
    struct timespec addition = {.tv_sec = spec1.tv_sec + spec2.tv_sec,
            .tv_nsec = spec1.tv_nsec + spec2.tv_nsec};
    if (addition.tv_nsec >= nsec_in_sec) {
        addition.tv_nsec -= nsec_in_sec;
        addition.tv_sec++;
    }
    return addition;
}

bool gte_timespec(const struct timespec spec1,
                  const struct timespec spec2) {
    if (spec1.tv_sec > spec2.tv_sec)
        return true;
    if (spec1.tv_sec < spec2.tv_sec)
        return false;
    if (spec1.tv_nsec > spec2.tv_nsec)
        return true;
    if (spec1.tv_nsec < spec2.tv_nsec)
        return false;
    return true;
}


double timespec_to_sec(struct timespec spec) {
    double sec = (double) spec.tv_sec;
    sec += ((double) spec.tv_nsec) / ((double) nsec_in_sec);
    return sec;
}

struct timespec milliseconds_to_timespec(long long ms) {
    long long nsec = ms * nsec_in_milliseconds;
    struct timespec spec = {.tv_sec = nsec / nsec_in_sec,
            .tv_nsec = nsec % nsec_in_sec};
    return spec;
}

long long timespec_to_milliseconds(struct timespec time) {
    long long ms = time.tv_sec * milliseconds_in_sec;
    ms += time.tv_nsec / nsec_in_milliseconds;
    return ms;
}

struct timespec microseconds_to_timespec(long long ms) {
    long long nsec = ms * nsec_in_microseconds;
    struct timespec spec = {.tv_sec = nsec / nsec_in_sec,
            .tv_nsec = nsec % nsec_in_sec};
    return spec;
}

long long timespec_to_microseconds(struct timespec time) {
    long long ms = time.tv_sec * microseconds_in_sec;
    ms += time.tv_nsec / nsec_in_microseconds;
    return ms;
}


struct file_queue;


struct coroutine_context {
    char *name;
    struct file_queue *shared_file_queue;
    int context_switch_count;

    struct timespec start_timespec;
    struct timespec quantum_soft_limit;
    struct timespec time_working;
};


struct coroutine_context *
create_coroutine_context(char *name, int quantum_soft_limit_microseconds, struct file_queue *shared_file_queue) {
    struct coroutine_context *context = malloc(sizeof(struct coroutine_context));

    context->name = name;
    context->quantum_soft_limit = microseconds_to_timespec(quantum_soft_limit_microseconds);
    context->shared_file_queue = shared_file_queue;
    struct timespec time_working = {.tv_sec = 0, .tv_nsec = 0};
    context->time_working = time_working;
    context->context_switch_count = 0;

    return context;
}

void dispose_of_coroutine_context(struct coroutine_context *context) {
    free(context->name);
    free(context);
}

struct single_sorted_file_data {
    int number_count;
    int *sorted_numbers;
};

void coro_yield_with_respect_to_quantum(struct coroutine_context *context) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    struct timespec time_spent_this_run = diff_timespec(now, context->start_timespec);
    if (gte_timespec(time_spent_this_run, context->quantum_soft_limit)) {
        context->context_switch_count += 1;
        context->time_working = add_timespec(context->time_working, time_spent_this_run);
        coro_yield();
        clock_gettime(CLOCK_MONOTONIC, &context->start_timespec);
    }
}

/*
 * Based on https://textbooks.cs.ksu.edu/cc310/7-searching-and-sorting/19-quicksort-pseudocode/
 */
int quick_sort_partition(int number_count, int *numbers, struct coroutine_context *context) {
    int pivot_value = numbers[number_count - 1];
    int pivot_index = 0;
    for (int i = 0; i < number_count; ++i) {
        if (numbers[i] <= pivot_value) {
            int temp = numbers[i];
            numbers[i] = numbers[pivot_index];
            numbers[pivot_index] = temp;
            pivot_index++;
        }
        coro_yield_with_respect_to_quantum(context);
    }
    return pivot_index - 1;

}

/*
 * Based on https://textbooks.cs.ksu.edu/cc310/7-searching-and-sorting/19-quicksort-pseudocode/
 */
int *get_sorted_inplace_numbers(int number_count, int *numbers, struct coroutine_context *context) {
    if (number_count <= 1)
        return numbers;
    int pivot_index = quick_sort_partition(number_count, numbers, context);
    get_sorted_inplace_numbers(pivot_index, numbers, context);
    get_sorted_inplace_numbers(number_count - pivot_index, numbers + pivot_index, context);
    return numbers;
}

struct single_sorted_file_data *
get_sorted_inplace_file_data(int number_count, int *numbers, struct coroutine_context *context) {
    struct single_sorted_file_data *data = malloc(sizeof(struct single_sorted_file_data));

    data->number_count = number_count;
    data->sorted_numbers = get_sorted_inplace_numbers(number_count, numbers, context);

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
coroutine_func_f(void *coroutine_context) {
    struct coroutine_context *context = coroutine_context;
    printf("coroutine %s starts\n", context->name);
    clock_gettime(CLOCK_MONOTONIC, &context->start_timespec);

    while (context->shared_file_queue->file_ptr < context->shared_file_queue->file_count) {
        int file_ptr = context->shared_file_queue->file_ptr;
        context->shared_file_queue->file_ptr++;

        char *file_name = context->shared_file_queue->file_names[file_ptr];
        int number_count = count_numbers_in_file(file_name);
        int *numbers = get_numbers_in_file(file_name, number_count);

        printf("coroutine %s starts sorting file %s (%d numbers detected)\n", context->name, file_name, number_count);
        context->shared_file_queue->sorted_files[file_ptr] = get_sorted_inplace_file_data(number_count, numbers,
                                                                                          context);
        printf("coroutine %s finishes sorting file %s\n", context->name, file_name);
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    struct timespec last_diff = diff_timespec(now, context->start_timespec);
    context->time_working = add_timespec(context->time_working, last_diff);
    printf("coroutine %s finished execution (context switches: %d; microseconds spent total: %lld)\n", context->name,
           context->context_switch_count, timespec_to_microseconds(context->time_working));

    dispose_of_coroutine_context(coroutine_context);
    return 0;
}

void output_merged_sorted_numbers_to_file(const struct file_queue *shared_file_queue, FILE *fp) {
    struct timespec merge_start;
    clock_gettime(CLOCK_MONOTONIC, &merge_start);

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

    struct timespec merge_end;
    clock_gettime(CLOCK_MONOTONIC, &merge_end);
    printf("total merge time (microseconds): %lld\n", timespec_to_microseconds(diff_timespec(merge_end, merge_start)));
}

int
main(int argc, char **argv) {
    struct timespec program_start;
    clock_gettime(CLOCK_MONOTONIC, &program_start);

    const int non_file_name_cli_arguments_count = 3;

    /* argv[0] is the executable */
    int target_latency;
    sscanf(argv[1], "%i", &target_latency);
    int coroutine_count;
    sscanf(argv[2], "%i", &coroutine_count);
    int quantum_soft_limit_microseconds = target_latency / coroutine_count;
    if (quantum_soft_limit_microseconds == 0) {
        printf("WARNING: because of chosen target latency and coroutine count, quantum soft limit for a single coroutine is zero, which might lead to undesired behavior in terms of context switch count!\n");
    }

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
        struct coroutine_context *coroutine_context = create_coroutine_context(strdup(name),
                                                                               quantum_soft_limit_microseconds,
                                                                               shared_file_queue);
        coro_new(coroutine_func_f, coroutine_context);
    }
    /* Wait for all the coroutines to end. */
    struct coro *c;
    while ((c = coro_sched_wait()) != NULL) {
        printf("Finished %d\n", coro_status(c));
        coro_delete(c);
    }
    /* All coroutines have finished. */

    FILE *fp = fopen("merged_tests.txt", "w");
    output_merged_sorted_numbers_to_file(shared_file_queue, fp);
    fclose(fp);

    dispose_of_file_queue(shared_file_queue);

    struct timespec program_end;
    clock_gettime(CLOCK_MONOTONIC, &program_end);
    printf("total work time (microseconds): %lld\n",
           timespec_to_microseconds(diff_timespec(program_end, program_start)));

    return 0;
}
