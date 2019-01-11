#include <libuvc/libuvc.h>
#include <sys/timeb.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

uint64_t start_time = time_ns();

uint64_t time_ns(){
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((uint64_t) ts.tv_sec * 1000000LL) + ((uint64_t)ts.tv_nsec / 1000LL);
}

void debug(char *str){
	uint64_t elapsed_time = time_ns() - start_time;
}

int main(int argc, char **argv) {
    uint64_t t = time_ns();
    printf("%" PRIu64 "\n", t);
}
