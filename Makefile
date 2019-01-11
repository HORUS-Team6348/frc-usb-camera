CC = gcc
CFLAGS = -I/usr/include/libusb-1.0 -luvc -lusb-1.0 -lturbojpeg -I/usr/include/x86_64-linux-gnu

test:
	$(CC) -O3 capture.c $(CFLAGS) -o capture

clean:
	rm capture