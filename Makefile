CC      ?= clang
CFLAGS  = -Wall -Wextra -O2 -std=c99 -pedantic
LDFLAGS = $(shell pkg-config --libs libusb-1.0 2>/dev/null || echo -lusb-1.0)
CFLAGS += $(shell pkg-config --cflags libusb-1.0 2>/dev/null)

TARGET  = freerkDumper
SRCS    = reconstruction.c rockchip_images.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
