#include <libuvc/libuvc.h>
#include <sys/timeb.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

uint64_t start_time;

uint64_t time_ns(){
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    return (start.tv_sec * 1e9) + start.tv_nsec;
}

void callback(uvc_frame_t *frame, void *ptr){

}


void debug(char *str){
	uint64_t elapsed_time = time_ns() - start_time;
    printf("[%f] %s \n", elapsed_time/1e9f, str);
}

int main(int argc, char **argv) {
    uvc_context_t *ctx;
    uvc_device_t *dev;
    uvc_device_handle_t *devh;
    uvc_stream_ctrl_t ctrl;
    uvc_error_t res;

    start_time = time_ns();

    res = uvc_init(&ctx, NULL);

    if (res < 0) {
        uvc_perror(res, "uvc_init");
        return res;
    }

    res = uvc_find_device(ctx, &dev, 0, 0, NULL);

    if (res < 0) {
        uvc_perror(res, "uvc_find_device"); /* no devices found */
    } else {
        debug("UVC device found");
        
        res = uvc_open(dev, &devh);

        if (res < 0) {
            uvc_perror(res, "uvc_open");
        } else {
            debug("UVC device opened");
            
            res = uvc_get_stream_ctrl_format_size(devh, &ctrl, UVC_FRAME_FORMAT_MJPEG, 1280, 720, 30);
            uvc_print_stream_ctrl(&ctrl, stderr);
            
            uvc_close(devh);
            debug("UVC device closed");
        }
    }

    debug("UVC initialized");
    uvc_exit(ctx);
    debug("UVC exited");
}
