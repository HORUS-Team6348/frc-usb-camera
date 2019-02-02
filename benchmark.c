#include <turbojpeg.h>
#include <sys/timeb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>


tjhandle tjpeg;
uint8_t *yuv_buffer;
uint64_t ctr = 0;
uint64_t acc_time = 0;

uint64_t time_ns() {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    return (start.tv_sec * 1e9) + start.tv_nsec;
}

void process_image(uint8_t *buf, uint64_t len) {
    ctr++;
    //printf("hello there! len=%i buf[0]=%x buf[1]=%x, buf[len-2]=%x, buf[len-1]=%x\n", len, buf[0], buf[1], buf[len-2], buf[len-1]);

    uint64_t start_time = time_ns();

    int res = tjDecompressToYUV2(tjpeg, buf, len, yuv_buffer, 0, 4, 0, TJFLAG_FASTDCT);

    uint64_t elapsed = time_ns() - start_time;

    acc_time += elapsed;

    //printf("time elapsed: %li us, ctr=%li\n", elapsed / 1000, ctr);
}

int main(int argc, char **argv) { 
    if(argc != 2) {
        fprintf(stderr, "usage: %s video.mjpg\n", argv[0]);
        exit(-1);
    }

    tjpeg       = tjInitDecompress();
    yuv_buffer  = calloc(1920*1080, 3);

    uint8_t *buffer = calloc(10000000, 1);

    if(buffer == NULL || yuv_buffer == NULL){
        fprintf(stderr, "error: could not allocate array\n");
        exit(-1);
    }

    FILE *fin = fopen(argv[1], "r");

    if(fin == NULL){
        fprintf(stderr, "error: could not open file\n");
        exit(-1);
    }

    uint64_t buffer_loc = 0;
    uint8_t byte;

    while(true)  {
        byte = fgetc(fin);
        
        if(feof(fin) != 0){
            printf("average time per frame: %d us\n", acc_time/(ctr*1000));
            exit(0);
        } 

        buffer[buffer_loc] = byte;
        buffer_loc++;

        if (byte == 0xFF && (buffer_loc > 10)) {
            uint8_t nextb = fgetc(fin);
            buffer[buffer_loc] = nextb;
            buffer_loc++;

            if (nextb == 0xD9) {
                process_image(buffer, buffer_loc);
                buffer_loc = 0;
            }
        }
    }
}

