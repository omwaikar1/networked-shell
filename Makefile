#
# Makefile - Networked Concurrent Shell (NCS)
#
# Targets:
#   make           - build both ncs-server and ncs-client into bin/
#   make server    - build only the server
#   make client    - build only the client
#   make clean     - remove all build artifacts
#
CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -pthread -Isrc -D_GNU_SOURCE
LDFLAGS  = -pthread

SRC_DIR  = src
BIN_DIR  = bin

SERVER_SRC = $(SRC_DIR)/server.c   \
             $(SRC_DIR)/session.c  \
             $(SRC_DIR)/commands.c \
             $(SRC_DIR)/messaging.c \
             $(SRC_DIR)/utils.c

CLIENT_SRC = $(SRC_DIR)/client.c   \
             $(SRC_DIR)/utils.c

SERVER_OBJ = $(SERVER_SRC:.c=.o)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)

all: server client

server: $(BIN_DIR)/ncs-server
client: $(BIN_DIR)/ncs-client

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/ncs-server: $(SERVER_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SERVER_OBJ) $(LDFLAGS)

$(BIN_DIR)/ncs-client: $(CLIENT_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f  $(SRC_DIR)/*.o
	rm -rf $(BIN_DIR)

.PHONY: all clean server client
