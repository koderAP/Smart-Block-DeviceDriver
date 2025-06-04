
// test_simpleblk.c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define DEVICE   "/dev/smart_block"
#define SECTOR   99       // sector index to test
#define NSECT    1         // number of sectors to read/write
#define SECT_SIZE 512      // must match your driver

int main(void) {
    int fd = open(DEVICE, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    size_t buf_size = NSECT * SECT_SIZE;
    printf("%d\n", sizeof(char));
    printf("%d\n", buf_size);
    char *write_buf = (char*)malloc(buf_size);
    char *read_buf  = (char*)malloc(buf_size);
    if (!write_buf || !read_buf) {
        fprintf(stderr, "malloc failure\n");
        return 2;
    }

    // Fill write_buf with a pattern
    for (size_t i = 0; i < buf_size; i++) {
        write_buf[i] = 'a';
    }

    // Write at offset = SECTOR * SECT_SIZE
    off_t offset = (off_t)SECTOR * SECT_SIZE;
    if (pwrite(fd, write_buf, buf_size, offset) != buf_size) {
        perror("pwrite");
        return 3;
    }
    printf("Wrote %zu bytes at sector %d\n", buf_size, SECTOR);

    close(fd);
    int fd1 = open(DEVICE, O_RDWR);

    // Clear read_buf and read back
    memset(read_buf, 0, buf_size);
    offset = (off_t)(SECTOR+1) * SECT_SIZE;
    if (pread(fd, read_buf, buf_size, offset) != buf_size) {
        perror("pread");
        return 4;
    }
    printf("Read  %zu bytes at sector %d\n", buf_size, SECTOR);

    // Verify
    if (memcmp(write_buf, read_buf, buf_size) == 0) {
        printf("Test PASSED: data matches\n");
    } else {
        fprintf(stderr, "Test FAILED: data mismatch!\n");
        // Show first bad byte
        for (size_t i = 0; i < buf_size; i++) {
            if (write_buf[i] != read_buf[i]) {
                fprintf(stderr,
                    "  byte %zu: wrote 0x%02x, read 0x%02x\n",
                    i, (unsigned char)write_buf[i], (unsigned char)read_buf[i]);
                break;
            }
        }
    }

    free(write_buf);
    free(read_buf);
    close(fd1);
    return 0;
}
