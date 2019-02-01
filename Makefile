CC = gcc
CFLAGS = -I/usr/include/libusb-1.0 -luvc -lusb-1.0 -lturbojpeg -I/usr/include/x86_64-linux-gnu -lavcodec -lavutil

test:
	$(CC) -O3 uvc_test.c $(CFLAGS) -o uvc_test

server:
	$(CC) -O3 udp_server.c -o udp_server

clean:
	rm uvc_test udp_server
