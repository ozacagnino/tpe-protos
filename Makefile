# Makefile para Trabajo Práctico Especial - Protocolos de Comunicación 2025/2
# SOCKSv5 Proxy Server con protocolo de gestión
#
# Uso:
#   make           - Compila servidor (socks5d) y cliente de gestión (client)
#   make socks5d   - Compila solo el servidor
#   make client    - Compila solo el cliente de gestión
#   make clean     - Elimina archivos compilados
#   make test      - Ejecuta tests
#   make all       - Compila todo

# Compilador y estándar (C11 como requiere el TP)
CC = gcc
CSTD = -std=c11

# Flags de compilación
# -Wall -Wextra -Werror: Tratamos todos los warnings como errores
# -pedantic: Cumplimiento estricto del estándar
# -D_POSIX_C_SOURCE=200809L: Para funciones POSIX necesarias
# -D_DEFAULT_SOURCE: Para funciones adicionales (getaddrinfo, etc)
CFLAGS = $(CSTD) -Wall -Wextra -pedantic -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -g

# Directorios
INCLUDE_DIR = include
SRC_DIR = src
SERVER_DIR = $(SRC_DIR)/server
CLIENT_DIR = $(SRC_DIR)/client
LIB_DIR = $(SRC_DIR)/lib
BUILD_DIR = build

# Includes
INCLUDES = -I$(INCLUDE_DIR)

# Flags de linkeo
# -pthread: Para soporte de threads (resolución DNS)
LDFLAGS = -pthread

# Archivos fuente de la librería (framework de la cátedra + utilidades)
LIB_SRCS = $(wildcard $(LIB_DIR)/*.c)
LIB_OBJS = $(patsubst $(LIB_DIR)/%.c,$(BUILD_DIR)/lib/%.o,$(LIB_SRCS))

# Archivos fuente del servidor
SERVER_SRCS = $(wildcard $(SERVER_DIR)/*.c)
SERVER_OBJS = $(patsubst $(SERVER_DIR)/%.c,$(BUILD_DIR)/server/%.o,$(SERVER_SRCS))

# Archivos fuente del cliente
CLIENT_SRCS = $(wildcard $(CLIENT_DIR)/*.c)
CLIENT_OBJS = $(patsubst $(CLIENT_DIR)/%.c,$(BUILD_DIR)/client/%.o,$(CLIENT_SRCS))

# Binarios de salida
SERVER_BIN = socks5d
CLIENT_BIN = client

# Targets principales
.PHONY: all clean test socks5d client dirs

all: dirs socks5d client

# Crear directorios de build
dirs:
	@mkdir -p $(BUILD_DIR)/lib
	@mkdir -p $(BUILD_DIR)/server
	@mkdir -p $(BUILD_DIR)/client

# Servidor SOCKS5
socks5d: dirs $(LIB_OBJS) $(SERVER_OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(SERVER_BIN) $(LIB_OBJS) $(SERVER_OBJS) $(LDFLAGS)
	@echo "==> Servidor SOCKS5 compilado: $(SERVER_BIN)"

# Cliente de gestión
client: dirs $(LIB_OBJS) $(CLIENT_OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(CLIENT_BIN) $(LIB_OBJS) $(CLIENT_OBJS) $(LDFLAGS)
	@echo "==> Cliente de gestión compilado: $(CLIENT_BIN)"

# Reglas de compilación para librería
$(BUILD_DIR)/lib/%.o: $(LIB_DIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Reglas de compilación para servidor
$(BUILD_DIR)/server/%.o: $(SERVER_DIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Reglas de compilación para cliente
$(BUILD_DIR)/client/%.o: $(CLIENT_DIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Limpieza
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
	@echo "==> Limpieza completada"

# Tests (por implementar)
test: all
	@echo "==> Ejecutando tests..."
	# Aquí irían los tests unitarios

# Información de ayuda
help:
	@echo "Trabajo Práctico Especial - Protocolos de Comunicación 2025/2"
	@echo ""
	@echo "Targets disponibles:"
	@echo "  all      - Compila servidor y cliente (default)"
	@echo "  socks5d  - Compila solo el servidor SOCKS5"
	@echo "  client   - Compila solo el cliente de gestión"
	@echo "  clean    - Elimina archivos compilados"
	@echo "  test     - Ejecuta tests"
	@echo "  help     - Muestra esta ayuda"
	@echo ""
	@echo "Uso del servidor:"
	@echo "  ./socks5d -h                    # Muestra ayuda"
	@echo "  ./socks5d -p 1080 -P 8080       # Puerto SOCKS y gestión"
	@echo "  ./socks5d -u user:pass          # Agrega usuario"
	@echo ""
	@echo "Uso del cliente:"
	@echo "  ./client -h                     # Muestra ayuda"
	@echo "  ./client -L 127.0.0.1 -P 8080   # Conecta al servidor de gestión"

# Regla para mostrar variables (debug)
show-vars:
	@echo "CC        = $(CC)"
	@echo "CFLAGS    = $(CFLAGS)"
	@echo "LDFLAGS   = $(LDFLAGS)"
	@echo "LIB_SRCS  = $(LIB_SRCS)"
	@echo "LIB_OBJS  = $(LIB_OBJS)"
	@echo "SERVER_SRCS = $(SERVER_SRCS)"
	@echo "SERVER_OBJS = $(SERVER_OBJS)"
	@echo "CLIENT_SRCS = $(CLIENT_SRCS)"
	@echo "CLIENT_OBJS = $(CLIENT_OBJS)"

