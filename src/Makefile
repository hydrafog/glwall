# /etc/dotfiles/nixos/shared/modules/glwall/src/Makefile

# Compiler and flags
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g
LDFLAGS = -lwayland-client -lwayland-egl -lEGL -lGL -lGLEW -lm

# --- Protocol Configuration ---
# Check for environment variables from shell.nix
ifeq ($(WAYLAND_PROTOCOLS_DIR),)
  $(error "WAYLAND_PROTOCOLS_DIR is not set. Please run from a nix-shell.")
endif
ifeq ($(WLR_PROTOCOLS_DIR),)
  $(error "WLR_PROTOCOLS_DIR is not set. Please run from a nix-shell.")
endif

# Use vpath to tell 'make' where to find protocol files.
vpath %.xml $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell $(WLR_PROTOCOLS_DIR)/unstable/wlr-layer-shell

# --- Protocol File Definitions ---
LAYER_SHELL_PROTOCOL = wlr-layer-shell-unstable-v1
XDG_SHELL_PROTOCOL = xdg-shell

# --- Generated Files ---
# These are the files that will be created by wayland-scanner
LAYER_SHELL_CLIENT_HEADER = $(LAYER_SHELL_PROTOCOL)-client-protocol.h
LAYER_SHELL_CODE = $(LAYER_SHELL_PROTOCOL)-protocol.c
XDG_SHELL_CLIENT_HEADER = $(XDG_SHELL_PROTOCOL)-protocol.h
XDG_SHELL_CODE = $(XDG_SHELL_PROTOCOL)-protocol.c

GENERATED_HEADERS = $(LAYER_SHELL_CLIENT_HEADER) $(XDG_SHELL_CLIENT_HEADER)
GENERATED_SOURCES = $(LAYER_SHELL_CODE) $(XDG_SHELL_CODE)

# --- Source and Object Files ---
SRCS = main.c utils.c wayland.c egl.c opengl.c $(GENERATED_SOURCES)
OBJS = $(SRCS:.c=.o)

# Target executable
TARGET = glwall

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "==> Linking executable: $@"
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Any .o file depends on all generated headers.
%.o: %.c $(GENERATED_HEADERS)
	@echo "==> Compiling C source: $<"
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Rules to generate protocol files ---

# Rule for wlr-layer-shell
$(LAYER_SHELL_CLIENT_HEADER): $(LAYER_SHELL_PROTOCOL).xml
	@echo "==> Generating client header from $<"
	@wayland-scanner client-header < $< > $@

$(LAYER_SHELL_CODE): $(LAYER_SHELL_PROTOCOL).xml
	@echo "==> Generating private code from $<"
	@wayland-scanner private-code < $< > $@

# Rule for xdg-shell
$(XDG_SHELL_CLIENT_HEADER): $(XDG_SHELL_PROTOCOL).xml
	@echo "==> Generating client header from $<"
	@wayland-scanner client-header < $< > $@

$(XDG_SHELL_CODE): $(XDG_SHELL_PROTOCOL).xml
	@echo "==> Generating private code from $<"
	@wayland-scanner private-code < $< > $@

# --- Cleanup --- 
clean:
	@echo "==> Cleaning project"
	rm -f $(TARGET) $(OBJS) $(GENERATED_HEADERS) $(GENERATED_SOURCES)