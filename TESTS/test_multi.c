#include "test.h"

void display_stats() {
    printf("Accessing device statistics...\n");
    FILE *file = fopen(PROC_STATS, "r");
    if (!file) {
        perror("Error opening statistics file");
        return;
    }

    char buffer[1024];
    size_t n;
    
    printf("\n--- Device Statistics ---\n");
    while ((n = fread(buffer, 1, sizeof(buffer) - 1, file)) > 0) {
        buffer[n] = '\0';
        fputs(buffer, stdout);
    }
    printf("-------------------------\n");

    fclose(file);
}

void trigger_flush(pid_t pid) {
    FILE *file = fopen(FLUSH, "w");
    if (!file) {
        perror("flush open failed");
        return;
    }
    fprintf(file, "%d", pid);
    fclose(file);
    printf("Flush initiated for PID %d\n", pid);
}

void update_cache_size(unsigned long size) {
    FILE *file = fopen(CACHE_SIZE, "w");
    if (!file) {
        perror("cache_size open failed");
        return;
    }
    fprintf(file, "%lu", size);
    fclose(file);
    printf("Cache size set to %lu bytes\n", size);
}

unsigned long read_cache_size() {
    FILE *file = fopen(CACHE_SIZE, "r");
    if (!file) {
        perror("cache_size read failed");
        return 0;
    }
    unsigned long size;
    fscanf(file, "%lu", &size);
    fclose(file);
    return size;
}

void* high_priority_writer(void *arg) {
    int id = *((int*)arg);
    setpriority(PRIO_PROCESS, 0, -5);
    printf("HP thread %d started (priority %d)\n", id, getpriority(PRIO_PROCESS, 0));

    int fd = open(BLOCK_DEVICE_PATH, O_WRONLY);
    if (fd < 0) {
        perror("HP open failed");
        return NULL;
    }

    unsigned char data[SECTOR_SIZE];
    memset(data, PATTERN_1 + id, SECTOR_SIZE);

    for (int i = 0; i < PER_THREAD_ITER && test_running; ++i) {
        off_t offset = (id * PER_THREAD_ITER + i) * SECTOR_SIZE;
        if (lseek(fd, offset, SEEK_SET) < 0) {
            perror("HP lseek");
            write_errors++;
            continue;
        }
        data[0] = (unsigned char)(i & 0xFF);
        if (write(fd, data, SECTOR_SIZE) != SECTOR_SIZE) {
            perror("HP write");
            write_errors++;
            continue;
        }

        pthread_mutex_lock(&counter_mutex);
        total_writes++;
        pthread_mutex_unlock(&counter_mutex);

        printf("HP thread %d wrote sector %ld\n", id, offset / SECTOR_SIZE);
        usleep(100000);
    }

    close(fd);
    printf("HP thread %d finished\n", id);
    return NULL;
}

void* low_priority_writer(void *arg) {
    int id = *((int*)arg);
    setpriority(PRIO_PROCESS, 0, 5);
    printf("LP thread %d started (priority %d)\n", id, getpriority(PRIO_PROCESS, 0));

    int fd = open(BLOCK_DEVICE_PATH, O_WRONLY);
    if (fd < 0) {
        perror("LP open failed");
        return NULL;
    }

    unsigned char data[SECTOR_SIZE];
    memset(data, PATTERN_3 + id, SECTOR_SIZE);

    int base = 1000 + id * PER_THREAD_ITER;
    for (int i = 0; i < PER_THREAD_ITER && test_running; ++i) {
        off_t offset = (base + i) * SECTOR_SIZE;
        if (lseek(fd, offset, SEEK_SET) < 0) {
            perror("LP lseek");
            write_errors++;
            continue;
        }
        data[0] = (unsigned char)(i & 0xFF);
        if (write(fd, data, SECTOR_SIZE) != SECTOR_SIZE) {
            perror("LP write");
            write_errors++;
            continue;
        }

        pthread_mutex_lock(&counter_mutex);
        total_writes++;
        pthread_mutex_unlock(&counter_mutex);

        printf("LP thread %d wrote sector %ld\n", id, offset / SECTOR_SIZE);
        usleep(100000);
    }

    close(fd);
    printf("LP thread %d finished\n", id);
    return NULL;
}

void* reader_thread(void *arg) {
    int id = *((int*)arg);
    printf("Reader %d started\n", id);

    int fd = open(BLOCK_DEVICE_PATH, O_RDONLY);
    if (fd < 0) {
        perror("Reader open failed");
        return NULL;
    }

    unsigned char data[SECTOR_SIZE];
    sleep(5);

    for (int i = 0; i < PER_THREAD_ITER * 2 && test_running; ++i) {
        int sector;
        if (i % 2 == 0) {
            sector = rand() % (HIGH_PRIO_THREADS * PER_THREAD_ITER);
        } else {
            sector = 1000 + rand() % (LOW_PRIO_THREADS * PER_THREAD_ITER);
        }

        off_t offset = sector * SECTOR_SIZE;
        if (lseek(fd, offset, SEEK_SET) < 0 || read(fd, data, SECTOR_SIZE) != SECTOR_SIZE) {
            perror("Reader error");
            __sync_fetch_and_add(&read_errors, 1);
            continue;
        }

        int mismatch = 0;
        unsigned char expected_first = 0, expected_fill = 0;

        if (sector < HIGH_PRIO_THREADS * PER_THREAD_ITER) {
            int t = sector / PER_THREAD_ITER;
            int it = sector % PER_THREAD_ITER;
            expected_first = (unsigned char)(it & 0xFF);
            expected_fill = (unsigned char)(PATTERN_1 + t);
        } else if (sector >= 1000 && sector < 1000 + LOW_PRIO_THREADS * PER_THREAD_ITER) {
            int t = (sector - 1000) / PER_THREAD_ITER;
            int it = (sector - 1000) % PER_THREAD_ITER;
            expected_first = (unsigned char)(it & 0xFF);
            expected_fill = (unsigned char)(PATTERN_3 + t);
        } else {
            pthread_mutex_lock(&counter_mutex);
            total_reads++;
            pthread_mutex_unlock(&counter_mutex);
            usleep(75000);
            continue;
        }

        if (data[0] != expected_first) {
            mismatch = 1;
            printf("ERROR R%d: sector %d [0]=0x%02X != 0x%02X\n", id, sector, data[0], expected_first);
        } else {
            for (int b = 1; b < SECTOR_SIZE; ++b) {
                if (data[b] != expected_fill) {
                    mismatch = 1;
                    printf("ERROR R%d: sector %d [%d]=0x%02X != 0x%02X\n", id, sector, b, data[b], expected_fill);
                    break;
                }
            }
        }

        if (mismatch) __sync_fetch_and_add(&read_errors, 1);

        pthread_mutex_lock(&counter_mutex);
        total_reads++;
        pthread_mutex_unlock(&counter_mutex);

        printf("Reader %d checked sector %d %s\n", id, sector, mismatch ? "(MISMATCH)" : "(OK)");
        usleep(75000);
    }

    close(fd);
    printf("Reader %d done\n", id);
    return NULL;
}

void run_dynamic_cache_test() {
    printf("\n--- Cache Resizing and Flush Test ---\n");

    unsigned long original = read_cache_size();
    printf("Original cache size: %lu\n", original);

    update_cache_size(original + 1024 * 1024);
    sleep(1);
    trigger_flush(getpid());
    display_stats();

    update_cache_size(original / 2);
    sleep(1);
    trigger_flush(getpid());
    display_stats();

    update_cache_size(original);
    printf("Cache size reset to original\n");
}

int main(void) {
    printf("Smart Block Device Driver Test Program\n");
    printf("======================================\n");

    srand(time(NULL));
    display_stats();
    run_dynamic_cache_test();

    pthread_t hp_threads[HIGH_PRIO_THREADS];
    pthread_t lp_threads[LOW_PRIO_THREADS];
    pthread_t rd_threads[READ_THREAD_COUNT];

    int hp_ids[HIGH_PRIO_THREADS];
    int lp_ids[LOW_PRIO_THREADS];
    int rd_ids[READ_THREAD_COUNT];

    printf("\n--- Launching Priority-Based Thread Test ---\n");

    for (int i = 0; i < HIGH_PRIO_THREADS; ++i) {
        hp_ids[i] = i;
        pthread_create(&hp_threads[i], NULL, high_priority_writer, &hp_ids[i]);
    }

    for (int i = 0; i < LOW_PRIO_THREADS; ++i) {
        lp_ids[i] = i;
        pthread_create(&lp_threads[i], NULL, low_priority_writer, &lp_ids[i]);
    }

    for (int i = 0; i < READ_THREAD_COUNT; ++i) {
        rd_ids[i] = i;
        pthread_create(&rd_threads[i], NULL, reader_thread, &rd_ids[i]);
    }

    printf("Running test for %d seconds...\n", DURATION);
    sleep(DURATION);
    test_running = 0;

    for (int i = 0; i < HIGH_PRIO_THREADS; ++i) pthread_join(hp_threads[i], NULL);
    for (int i = 0; i < LOW_PRIO_THREADS; ++i)  pthread_join(lp_threads[i], NULL);
    for (int i = 0; i < READ_THREAD_COUNT; ++i)     pthread_join(rd_threads[i], NULL);

    printf("\n--- Test Summary ---\n");
    printf("Total writes: %d\n", total_writes);
    printf("Total reads:  %d\n", total_reads);
    printf("Write errors: %d\n", write_errors);
    printf("Read  errors: %d\n", read_errors);

    display_stats();
    return 0;
}
