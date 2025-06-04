#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/fs.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>

#define BLOCK_DEVICE_PATH         "/dev/smart_block"
#define SECTOR_SIZE         512
#define NUM_SECTORS         4096
#define PROC_STATS          "/proc/smart_block/stats"
#define FLUSH          "/proc/smart_block/flush"
#define CACHE_SIZE     "/proc/smart_block/resize_cache"
#define VIEW_CACHE      "/proc/smart_block/cache"

#define PATTERN_1           0xA1
#define PATTERN_2           0xB1
#define PATTERN_3           0xC1
#define PATTERN_4           0xD1
#define HIGH_PRIO_THREADS   4
#define LOW_PRIO_THREADS    6
#define READ_THREAD_COUNT        4
#define PER_THREAD_ITER   20
#define DURATION           15  

volatile int test_running = 1;
int total_writes = 0;
int total_reads  = 0;
int read_errors  = 0;
int write_errors = 0;
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;



