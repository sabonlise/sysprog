#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include "libcoro.h"

#define US_TO_MS(us) ((us / 1000))

struct File {
    int *arr;
    char *name;
    int size;
    int id;
    uint64_t work_time;
    uint64_t last_yield_time;
};

static long target_latency;
static long time_quantum;

struct File *files;

void write_to_file(const char *file_name, int *arr, int arr_size) {
    FILE *file_p = fopen(file_name, "w");

    for (int i = 0; i < arr_size; i++) {
        fprintf(file_p, "%d ", arr[i]);
    }

    fclose(file_p);
}

static inline uint64_t get_monotonic_milliseconds(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    uint64_t ms = ((uint64_t) ts.tv_sec) * 1000 + ((uint64_t) ts.tv_nsec) / 1000000;
    return ms;
}

static inline uint64_t get_monotonic_microseconds(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    uint64_t us = ((uint64_t)ts.tv_sec) * 1000000 + ((uint64_t)ts.tv_nsec) / 1000;
    return us;
}

static inline uint64_t last_yield_time_diff(uint64_t time, int id) {
    return time - files[id].last_yield_time;
}

void coro_update(uint64_t time, int id) {
    files[id].work_time += last_yield_time_diff(time, id);
    files[id].last_yield_time = get_monotonic_microseconds();
}

void swap(int *num1, int *num2) {
    int tmp = *num1;
    *num1 = *num2;
    *num2 = tmp;
}

void quick_sort(int *arr, int low, int high, int id) {
    int mid = low + (high - low) / 2;
    int pivot = arr[mid];
    int i = low, j = high;

    while (i <= j) {
        while (arr[i] < pivot) i++;
        while (arr[j] > pivot) j--;

        if (i <= j) {
            swap(&arr[i], &arr[j]);
            i++; j--;
        }

        uint64_t us_now = get_monotonic_microseconds();

        // After exceeding allowed time quantum, yield and update coroutine information
        if (time_quantum <= last_yield_time_diff(us_now, id)) {
            coro_yield();
            coro_update(us_now, id);
        }
    }

    if (low < j) quick_sort(arr, low, j, id);
    if (i < high) quick_sort(arr, i, high, id);
}


void merge_sort(struct File* _files, int num_arrays, int *result, int maxsize) {
    int min, pos;
    int *current_pos = calloc(num_arrays, sizeof(int));

    for (int i = 0; i < maxsize; i++) {
        min = INT32_MAX;
        pos = -1;

        for(int j = 0; j < num_arrays; j++) {
            if (current_pos[j] >= files[j].size) continue;
            if (_files[j].arr[current_pos[j]] < min) {
                pos = j;
                min = _files[j].arr[current_pos[j]];
            }
        }

        if (pos < 0) break;

        current_pos[pos]++;
        result[i] = min;
    }
    free(current_pos);
}

static void sort_file(struct File *file) {
    int count = 0, tmp, i = 0;
    char* name = strdup(file->name);

    printf("%s: entered function (%s)\n", name, file->name);

    FILE *file_p = fopen(name, "r");
    while(fscanf(file_p, "%d", &tmp) == 1) count++;

    file->size = count;
    file->arr = (int *) realloc(file->arr, sizeof(int) * count);

    rewind(file_p);

    while(i < count && (fscanf(file_p, "%d", &file->arr[i++]) == 1));
    fclose(file_p);

    // Storing last yield time for current processed file right before performing quick sort for accurate results
    file->last_yield_time = get_monotonic_microseconds();
    quick_sort(file->arr, 0, count - 1, file->id);

    file->name = strdup(name);
    write_to_file(file->name, file->arr, count);

    free(name);
}


static int coroutine_sort_f(void *context) {
    struct coro *this = coro_this();

    struct File *file = (struct File *) context;

    printf("Started coroutine %s\n", file->name);

    sort_file(file);

    uint64_t time_passed = file->work_time;
    printf("%s: switch count %lld, work time: %lu ms (%lu microseconds)\n", file->name,
           coro_switch_count(this), US_TO_MS(time_passed), time_passed);

    return 0;

}

// ./main 6000 test1.txt test2.txt test3.txt test4.txt test5.txt test6.txt
// 6 files, 6000 / 6 = 1000 us = 1 ms roughly given to one coroutine
// so switch count in this case = work time in ms
int main(int argc, char **argv)
{
    printf("Argc: %d\n", argc);
    target_latency = atoi(argv[1]);

    coro_sched_init();
    int file_count = argc - 2;
    // Work time quantum that is allowed for each coroutine before switching
    time_quantum = target_latency / file_count;

    files = (struct File*) malloc(sizeof(struct File) * file_count);
    uint64_t start_time = get_monotonic_milliseconds();

    for (int i = 2; i < argc; i++) {
        files[i - 2].name = strdup(argv[i]);
        files[i - 2].arr = (int *) malloc(sizeof(int));
        files[i - 2].id = i - 2;
        files[i - 2].size = 0;
        files[i - 2].work_time = 0;

        coro_new(coroutine_sort_f, (void *) &files[i - 2]);
    }

    struct coro *c;
    while ((c = coro_sched_wait()) != NULL) {
        printf("Finished %d\n", coro_status(c));
        coro_delete(c);
    }

    int resulting_size = 0;
    for (int i = 0; i < file_count; i++) {
        resulting_size += files[i].size;
    }

    int *result = calloc(resulting_size + 1, sizeof(int));

    (void) merge_sort(files, file_count, result, resulting_size);
    write_to_file("result.txt", result, resulting_size);

    uint64_t end_time = get_monotonic_milliseconds();

    double total_time = (double) (end_time - start_time) / 1000;
    printf("Seconds passed %f (%lu ms)", total_time, end_time - start_time);

    for (int i = 0; i < file_count; i++) {
        free(files[i].arr);
        free(files[i].name);
    }
    
    free(files);
    free(result);

	return 0;
}
