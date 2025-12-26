CC = gcc
CFLAGS = -Wall -Wextra -Werror -O3 -march=native -pthread
CFLAGS += -std=c11
CFLAGS += -I../../shared/math
CFLAGS += $(shell pkg-config --cflags gl)
CFLAGS += $(shell pkg-config --cflags glx)
CFLAGS += $(shell pkg-config --cflags egl)
CFLAGS += $(shell pkg-config --cflags xrandr 2>/dev/null || echo "-I/usr/include/X11/extensions")
CFLAGS += $(shell pkg-config --cflags libdrm 2>/dev/null || echo "-I/usr/include/libdrm")

LDFLAGS = -pthread
LDFLAGS += $(shell pkg-config --libs gl)
LDFLAGS += $(shell pkg-config --libs glx)
LDFLAGS += $(shell pkg-config --libs egl)
LDFLAGS += -lX11 -lXext -lXrandr
LDFLAGS += $(shell pkg-config --libs libdrm 2>/dev/null || echo "-ldrm")

TARGET = breezy_x11_renderer
SOURCES = breezy_x11_renderer.c drm_capture.c imu_reader.c shader_loader.c opengl_context.c logging.c
SHARED_MATH_SOURCES = ../../shared/math/breezy_math.c
SHARED_MATH_OBJECTS = $(SHARED_MATH_SOURCES:.c=.o)
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJECTS) $(SHARED_MATH_OBJECTS)
	$(CC) $(OBJECTS) $(SHARED_MATH_OBJECTS) -o $(TARGET) $(LDFLAGS) -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(SHARED_MATH_OBJECTS) $(TARGET)

install: $(TARGET)
	install -D -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/$(TARGET)
