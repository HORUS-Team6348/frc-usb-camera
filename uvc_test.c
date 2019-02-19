#include <libavcodec/avcodec.h>
#include "libuvc/libuvc.h"
#include <libavutil/opt.h>
#include <turbojpeg.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

int width, height, fps;

#define PORT 1188
#define BUFSIZE 512
#define MTU 491

#define NEGOTIATION 1
#define RESPONSE    2
#define FRAME       3
#define ACK         4
#define FINISH      5
#define CHANGE      6

uint64_t frame_counter = 0;
uint64_t sequence_counter = 0;
uint64_t dropped_counter = 0;
uint64_t frame_times = 0;
uint64_t frame_processing_times = 0;
uint64_t prev_frame_time = 0;
uint64_t data_bytes = 0;
uint64_t h264_bytes = 0;
uint64_t jpeg_time = 0;
uint64_t h264_time = 0;
uint64_t network_time = 0;
uint64_t last_segment_send_time = 0;
int64_t avg_interarrival_time = 0;
int64_t allowed_to_send = INT_MAX;
uint64_t last_acked_frame = 0;
uint8_t consecutive_skips = 0;
uint8_t camera_id = 0;
uint8_t lock = 0;

float crf = 23;
int overrun_counter = 0;

tjhandle tj_decompressor;

uvc_context_t *ctx;
uvc_error_t res;
uvc_device_t *dev_a;
uvc_device_t *dev_b;
uvc_device_t *dev_c;
uvc_device_handle_t *devh_a;
uvc_device_handle_t *devh_b;
uvc_device_handle_t *devh_c;
uvc_stream_ctrl_t ctrl_a;
uvc_stream_ctrl_t ctrl_b;
uvc_stream_ctrl_t ctrl_c;

AVCodec *codec;
AVFrame *avframe;
AVPacket *pkt;
AVCodecContext *avctx = NULL;


struct sockaddr_in serveraddr, clientaddr;
int sockfd;
void *netbuf;
uint8_t *framebuf;
socklen_t clientlen = sizeof clientaddr;

typedef struct negotiation_packet {
  uint8_t type;
  uint16_t width;
  uint16_t height;
  uint16_t fps;
} negotiation_packet;

typedef struct ack_packet {
  uint8_t type;
  uint32_t frame_idx;
  int32_t interarrival_time;
} ack_packet;

typedef struct change_packet {
  uint8_t type;
  uint8_t camera_id;
} change_packet;

typedef struct response_packet {
  uint8_t type;
  uint8_t success;
} response_packet;

typedef struct finish_packet {
  uint8_t type;
} finish_packet;

uint64_t get_ns(){
  struct timespec start;
  clock_gettime(CLOCK_MONOTONIC, &start);
  return (start.tv_sec * 1e9) + start.tv_nsec;
}

void die(char *s){
  perror(s);
  exit(1);
}

void jpeg_to_yuv422(void *src, size_t bytes){
  unsigned char *data_arrs[] = {avframe->data[0], avframe->data[1], avframe->data[2]};
  tjDecompressToYUVPlanes(tj_decompressor, src, bytes, data_arrs, width, NULL, height, TJFLAG_FASTDCT);
}

void network_send_frame(uint32_t frame_idx, uint8_t *data, int len){
  uint64_t start = get_ns();
  uint16_t grace_period = (start/1e3) - last_segment_send_time;

  int seq_total = len / MTU;
  if (len % MTU != 0){
    seq_total += 1;
  }

  int seq_idx = 0;
  int already_sent = 0;

  framebuf[0] = FRAME;
  framebuf[1] = frame_idx >> 24;
  framebuf[2] = frame_idx >> 16;
  framebuf[3] = frame_idx >> 8;
  framebuf[4] = frame_idx;
  framebuf[9] = seq_total >> 24;
  framebuf[10] = seq_total >> 16;
  framebuf[11] = seq_total >> 8;
  framebuf[12] = seq_total;
  framebuf[13] = grace_period >> 24;
  framebuf[14] = grace_period >> 16;
  framebuf[15] = grace_period >> 8;
  framebuf[16] = grace_period;

  while (already_sent < len) {
    framebuf[5] = seq_idx >> 24;
    framebuf[6] = seq_idx >> 16;
    framebuf[7] = seq_idx >> 8;
    framebuf[8] = seq_idx;

    int packet_len;
    if((len-already_sent) > MTU){
      packet_len = MTU + 17;
    } else {
      packet_len = (len-already_sent) + 17;
    }

    memcpy(framebuf+17, data+already_sent, packet_len-17);
    int res = sendto(sockfd, framebuf, packet_len, 0, (struct sockaddr *) &clientaddr, clientlen);

    if(res == -1){
      die("error while sending");
    }

    //printf("wrote frame %i seq %i/%i (%i/%i written)\n\n", frame_idx, seq_idx, seq_total, already_sent+(packet_len-17), len);

    already_sent += MTU;
    seq_idx += 1;

  }

  last_segment_send_time = get_ns()/1000;
  network_time += get_ns() - start;
}

void set_crf(){
  char crfs[10];
  if(crf > 32){
    crf = 32;
  } else if(crf < 0){
    crf = 0;
  }
  sprintf(crfs,"%i",(int)crf);
  av_opt_set(avctx->priv_data, "crf", crfs, 0);
}

void ffmpeg_encode_frame(uint64_t pts, bool flush){
  uint32_t ret;
  avframe->pts = pts;
  
  if(pts % 100 == 0){
    avframe->pict_type = AV_PICTURE_TYPE_I;
  }

  if(flush){
    avframe = NULL;
  }

  ret = avcodec_send_frame(avctx, avframe);
  if (ret < 0) {
    fprintf(stderr, "error sending a frame for encoding\n");
    exit(1);
  }

  while (ret >= 0) {
    ret = avcodec_receive_packet(avctx, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        break;
    else if (ret < 0) {
        fprintf(stderr, "error during encoding\n");
        exit(1);
    }
    h264_bytes += pkt->size;
    if (!flush){
      if(pts == 1){
        network_send_frame(pts, pkt->data, pkt->size); //redundancy, it's the first frame and it contains important headers
      }
      network_send_frame(pts, pkt->data, pkt->size);

      if(allowed_to_send > pkt->size){
        crf -= 0.04;
      } else {
        crf += 0.04;
      }
      set_crf();
    }
    av_packet_unref(pkt);
  }
}

void cb(uvc_frame_t *frame, void *ptr){
  lock = 1;
  uint64_t start, end, elapsed;
  uint32_t ret, pkt_counter;

  start  = get_ns();
  //start frame processing

  printf("\033[2Areceived frame %d (%" PRIu64 ") from camera %i (%zu bytes) after %" PRIu64 " ns (%.2f est fps)\n",
    frame->sequence, frame_counter+1, camera_id, frame->data_bytes, start-prev_frame_time, (1.0e9/(start-prev_frame_time)));

  frame_times += start-prev_frame_time;
  data_bytes += frame->data_bytes;
  prev_frame_time = start;
  frame_counter++;
  dropped_counter = frame->sequence - frame_counter;
  sequence_counter = frame->sequence;


  jpeg_to_yuv422(frame->data, frame->data_bytes);
  jpeg_time += get_ns() - start;
  ffmpeg_encode_frame(frame_counter, false);
  h264_time += get_ns() - start;


  //end frame processing
  end = get_ns();
  elapsed = end - start;
  if(elapsed > (1e9 / fps)) {
    overrun_counter += 1;
  }
  frame_processing_times += elapsed;
  double pct = (elapsed * fps / 1e7);
  printf("\033[2Kframe processed in %" PRIu64 " ns (%f%% of available time) (encoded with crf: %i)\n", elapsed, pct, (int) crf);
  lock = 0;
}

void cba(uvc_frame_t *frame, void *ptr){
  if(camera_id == 0 && lock != 1){
    cb(frame, ptr);
  }
}

void cbb(uvc_frame_t *frame, void *ptr){
  if(camera_id == 1 && lock != 1){
    cb(frame, ptr);
  }
}

void cbc(uvc_frame_t *frame, void *ptr){
  if(camera_id == 2 && lock != 1){
    cb(frame, ptr);
  }
}


int ffmpeg_encoder_start(int width, int height, int fps){
  int ret;
  avcodec_register_all();
  //av_log_set_level(AV_LOG_QUIET);

  codec = avcodec_find_encoder(AV_CODEC_ID_H264);

  if (!codec) {
    fprintf(stderr, "error: codec not found\n");
    return 1;
  }

  avctx = avcodec_alloc_context3(codec);

  if(!avctx){
    fprintf(stderr, "error: could not allocate video codec context\n");
    return 1;
  }

  avctx->width = width;
  avctx->height = height;
  avctx->time_base = (AVRational){1,fps};
  avctx->pix_fmt = AV_PIX_FMT_YUV422P;
  //avctx->rc_buffer_size = 1e6;
  //avctx->rc_max_rate = 10e6;

  av_opt_set(avctx->priv_data, "preset", "ultrafast", 0);
  av_opt_set(avctx->priv_data, "tune", "zerolatency", 0);
  av_opt_set(avctx->priv_data, "crf", "23", 0);

  ret = avcodec_open2(avctx, codec, NULL);

  if (ret < 0) {
    fprintf(stderr, "error: could not open codec: %s\n", av_err2str(ret));
    return 1;
  }

  pkt = av_packet_alloc();

  if (!pkt) {
    exit(1);
  }


  avframe = av_frame_alloc();

  if (!avframe) {
    fprintf(stderr, "error: could not allocate video frame\n");
    return 1;
  }

  avframe->format = avctx->pix_fmt;
  avframe->width  = width;
  avframe->height = height;
  avframe->linesize[0] = width;
  avframe->linesize[1] = width/2;
  avframe->linesize[2] = width/2;

  ret = av_frame_get_buffer(avframe, 32);

  if (ret < 0) {
    fprintf(stderr, "error: could not allocate the video frame data\n");
    return 1;
  }


}

int turbojpeg_decoder_start(){
  tj_decompressor = tjInitDecompress();
  return 0;
}

uvc_error_t uvc_setup_dev(){
  res = uvc_init(&ctx, NULL);

  if (res < 0) {
    uvc_perror(res, "uvc_init");
    return res;
  }

  printf("libuvc initialized correctly\n");

  uvc_device_t **udl;

  res = uvc_get_device_list(ctx, &udl);

  if (res < 0) {
    uvc_perror(res, "uvc_get_device_list");
    return res;
  }

  printf("found devices\n");

  res = uvc_open(udl[0], &devh_a);
  res = uvc_open(udl[1], &devh_b);

  uvc_free_device_list(udl, 0);

  if (res < 0) {
    uvc_perror(res, "uvc_open");
    return res;
  }

  printf("device opened correctly\n");
}

uvc_error_t uvc_setup_stream(int width, int height, int fps){
  res = uvc_get_stream_ctrl_format_size(devh_a, &ctrl_a, UVC_FRAME_FORMAT_MJPEG, width, height, fps);
  res = uvc_get_stream_ctrl_format_size(devh_b, &ctrl_b, UVC_FRAME_FORMAT_MJPEG, width, height, fps);

  if (res < 0) {
    uvc_perror(res, "uvc_get_stream_ctrl_format_size");
    return res;
  }
}

uvc_error_t start_stream(){
  res = uvc_start_streaming(devh_a, &ctrl_a, cba, 0, 0);
  res = uvc_start_streaming(devh_b, &ctrl_b, cbb, 0, 0);

  prev_frame_time = get_ns();

  if (res < 0) {
    uvc_perror(res, "uvc_start_streaming");
    return res;
  }

  printf("streaming...\n\n\n\n");

  res = uvc_set_ae_priority(devh_a, 1);
  res = uvc_set_ae_priority(devh_b, 1);

}

void stop_stream(){
    uvc_stop_streaming(devh_a);
    uvc_stop_streaming(devh_b);
    ffmpeg_encode_frame(frame_counter+1, true);
    printf("\n");
}

void app_print_stats(){
  printf("stats: \n");
  printf("\tavg frame time: %.0f ns (%.2f est fps)\n", frame_times/((double)frame_counter), ((double)frame_counter*1e9)/frame_times);
  printf("\tavg frame processing time: %" PRIu64  " ns (%f%% of available time)\n\n", frame_processing_times/frame_counter, 100*frame_processing_times/(33333333.0*frame_counter));

  printf("\tframe processing time overruns: %i (%.2f%% of frames)\n", overrun_counter, 100*overrun_counter/((double)frame_counter));
  printf("\tdropped frames: %" PRIu64 " (%.2f%% of camera frames)\n\n", dropped_counter, 100*dropped_counter/((double)sequence_counter));

  printf("\tavg jpeg frame size: %" PRIu64 " bytes\n", data_bytes/frame_counter);
  printf("\tavg jpeg bandwidth: %.2f mbit/s\n\n", (data_bytes*1e3*8)/((double)frame_times));

  printf("\tyuv422p frame size: %i bytes\n", width*height*2);
  printf("\tyuv422p bandwidth: %.2f mbit/s\n\n", (width*height*fps*2*8.0/1e6));

  printf("\tavg h264 frame size: %" PRIu64 " bytes\n", h264_bytes/frame_counter);
  printf("\tavg h264 bandwidth: %.2f mbit/s\n\n", (h264_bytes*1e3*8)/((double)frame_times));

  printf("\tavg pct time on jpeg decode: %.2f%%\n", 100*jpeg_time/((double)frame_times));
  printf("\tavg pct time on h264 encode: %.2f%%\n", (100*(h264_time-jpeg_time-network_time))/((double)frame_times));
  printf("\tavg pct time on network i/o: %.2f%%\n", (100*(network_time))/((double)frame_times));
}

void app_cleanup(){
  uvc_close(devh_a);
  uvc_close(devh_b);
  uvc_unref_device(dev_a);
  uvc_unref_device(dev_b);
  uvc_exit(ctx);
  avcodec_free_context(&avctx);
}

void setup_network(){
  int optval, res;

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);

  if(sockfd == -1){
    die("error: could not create socket");
  }

  optval = 1;
  res = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

  if(res == -1){
    die("error: could not set option SO_REUSEADDR");
  }

  bzero((char *)&serveraddr, sizeof(serveraddr)); //zero out server structure
  serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serveraddr.sin_port = htons((unsigned short)PORT);

  res = bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr));

  if(res == -1){
    die("error: could not bind to port");
  }

  netbuf = calloc(BUFSIZE, 1);
  framebuf = calloc(MTU+13, 1);

  printf("listening on udp port %i\n", PORT);
}

void decode_negotiation_packet(unsigned char *buf, negotiation_packet *packet){
  packet->type = buf[0];
  packet->width = (buf[1] << 8) | buf[2];
  packet->height = (buf[3] << 8) | buf[4];
  packet->fps = (buf[5] << 8) | buf[6];
}

void decode_ack_packet(unsigned char *buf, ack_packet *packet){
  packet->type = buf[0];
  packet->frame_idx = (buf[1] << 24) | (buf[2] << 16) | (buf[3] << 8) | buf[4];
  packet->interarrival_time = (buf[5] << 24) | (buf[6] << 16) | (buf[7] << 8) | buf[8];
}

void decode_change_packet(unsigned char *buf, change_packet *packet){
  packet->type = buf[0];
  packet->camera_id = buf[1];
}

void wait_for_negotiation(int *width, int *height, int* fps){
  printf("waiting for client request...\n");
  while(1){
    res = recvfrom(sockfd, netbuf, BUFSIZE, 0, (struct sockaddr *) &clientaddr, &clientlen);

    if(res == -1){
      die("error: could not receive packet");
    }

    uint8_t *nbuf = netbuf; //void pointer cast

    uint8_t type = nbuf[0];

    if(type == NEGOTIATION){
      negotiation_packet npacket;
      decode_negotiation_packet(netbuf, &npacket);
      *width = npacket.width;
      *height = npacket.height;
      *fps = npacket.fps;
      printf("received client request: %ix%i at %i fps\n", *width, *height, *fps);
      return;
    }
  }
}

void negotiation_success(){
  response_packet rpacket;
  rpacket.type = RESPONSE;
  rpacket.success = 1;
  sendto(sockfd, &rpacket, sizeof(response_packet), 0, (struct sockaddr *) &clientaddr, clientlen);
  last_segment_send_time = get_ns()/1000;
}

void finish_connection(){
  finish_packet fpacket;
  fpacket.type = FINISH;
  sendto(sockfd, &fpacket, sizeof(finish_packet), 0, (struct sockaddr *) &clientaddr, clientlen);
}

void control_loop(){
  while(true){
    res = recvfrom(sockfd, netbuf, BUFSIZE, 0, (struct sockaddr *) &clientaddr, &clientlen);

    if(res == -1){
      die("error: could not receive packet");
    }

    uint8_t *nbuf = netbuf; //void pointer cast
    uint8_t type = nbuf[0];

    if(type == ACK){
      ack_packet apacket;
      decode_ack_packet(nbuf, &apacket);
      last_acked_frame = apacket.frame_idx;
      avg_interarrival_time = (0.9*avg_interarrival_time) + (0.1*apacket.interarrival_time);
      int64_t packets_in_flight_limit = (100/(avg_interarrival_time/1e3));
      uint64_t current_frames_if = frame_counter - last_acked_frame;
      uint64_t aprox_bytes_if = current_frames_if * (h264_bytes/frame_counter);
      allowed_to_send = ((MTU+17)*packets_in_flight_limit) - aprox_bytes_if;
    } else if (type == CHANGE) {
      change_packet cpacket;
      decode_change_packet(nbuf, &cpacket);
      camera_id = cpacket.camera_id;
      printf("changed camera to %i", camera_id);
    } else if(type == FINISH) {
      stop_stream();
      finish_connection();
      app_print_stats();
      app_cleanup();
      exit(0);
    }
  }
}


int main(int argc, char **argv){
  int err;
  freopen("/dev/null", "w", stderr);

  setup_network();
  wait_for_negotiation(&width, &height, &fps);

  err = ffmpeg_encoder_start(width, height, fps);
  err = turbojpeg_decoder_start();

  res = uvc_setup_dev();
  res = uvc_setup_stream(width, height, fps);
  res = start_stream();

  negotiation_success();

  control_loop();

  stop_stream();

  finish_connection();

  printf("done streaming. cleaning up...\n");

  app_print_stats();
  app_cleanup();

  printf("done.\n");
}
