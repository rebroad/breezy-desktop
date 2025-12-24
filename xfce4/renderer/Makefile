CC = gcc
CFLAGS = -Wall -Wextra -O3 -march=native -pthread
CFLAGS += -std=c11
CFLAGS += $(shell pkg-config --cflags gl)
CFLAGS += $(shell pkg-config --cflags glx)
CFLAGS += $(shell pkg-config --cflags egl)
CFLAGS += $(shell pkg-config --cflags libdrm 2>/dev/null || echo "-I/usr/include/libdrm")

LDFLAGS = -pthread
LDFLAGS += $(shell pkg-config --libs gl)
LDFLAGS += $(shell pkg-config --libs glx)
LDFLAGS += $(shell pkg-config --libs egl)
LDFLAGS += -lX11 -lXext
LDFLAGS += $(shell pkg-config --libs libdrm 2>/dev/null || echo "-ldrm")

TARGET = breezy_xfce4_renderer
SOURCES = breezy_xfce4_renderer.c drm_capture.c imu_reader.c shader_loader.c opengl_context.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install -D -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/$(TARGET)
