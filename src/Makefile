CC = gcc
PKGS = wayland-client wayland-egl egl gl glew libpulse-simple libpulse libevdev libpng
CFLAGS = -std=c11 -Wall -Wextra -Werror -Wpedantic -g $(shell pkg-config --cflags $(PKGS)) $(EXTRA_CFLAGS)
LDFLAGS = $(shell pkg-config --libs $(PKGS)) -lm

ifeq ($(WAYLAND_PROTOCOLS_DIR),)
  $(error "WAYLAND_PROTOCOLS_DIR is not set. Please run from a nix flake.")
endif
ifeq ($(WLR_PROTOCOLS_DIR),)
  $(error "WLR_PROTOCOLS_DIR is not set. Please run from a nix flake.")
endif

vpath %.xml $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell $(WLR_PROTOCOLS_DIR)/unstable

LAYER_SHELL_PROTOCOL = wlr-layer-shell-unstable-v1
XDG_SHELL_PROTOCOL = xdg-shell

LAYER_SHELL_CLIENT_HEADER = $(LAYER_SHELL_PROTOCOL)-client-protocol.h
LAYER_SHELL_CODE = $(LAYER_SHELL_PROTOCOL)-protocol.c
XDG_SHELL_CLIENT_HEADER = $(XDG_SHELL_PROTOCOL)-protocol.h
XDG_SHELL_CODE = $(XDG_SHELL_PROTOCOL)-protocol.c

GENERATED_HEADERS = $(LAYER_SHELL_CLIENT_HEADER) $(XDG_SHELL_CLIENT_HEADER)
GENERATED_SOURCES = $(LAYER_SHELL_CODE) $(XDG_SHELL_CODE)

SRCS = main.c utils.c wayland.c egl.c opengl.c audio.c input.c image.c pipeline.c slang_process.c $(GENERATED_SOURCES)
OBJS = $(SRCS:.c=.o)

TARGET = glwall

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "==> Linking executable: $@"
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c $(GENERATED_HEADERS)
	@echo "==> Compiling C source: $<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(LAYER_SHELL_CLIENT_HEADER): $(LAYER_SHELL_PROTOCOL).xml
	@echo "==> Generating client header from $<"
	@wayland-scanner client-header < $< > $@

$(LAYER_SHELL_CODE): $(LAYER_SHELL_PROTOCOL).xml
	@echo "==> Generating private code from $<"
	@wayland-scanner private-code < $< > $@

$(XDG_SHELL_CLIENT_HEADER): $(XDG_SHELL_PROTOCOL).xml
	@echo "==> Generating client header from $<"
	@wayland-scanner client-header < $< > $@

$(XDG_SHELL_CODE): $(XDG_SHELL_PROTOCOL).xml
	@echo "==> Generating private code from $<"
	@wayland-scanner private-code < $< > $@

clean:
	@echo "==> Cleaning project"
	rm -f $(TARGET) $(OBJS) $(GENERATED_HEADERS) $(GENERATED_SOURCES)