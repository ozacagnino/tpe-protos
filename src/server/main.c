/**
 * main.c - Servidor proxy SOCKS5 concurrente
 *
 * Trabajo Práctico Especial - Protocolos de Comunicación 2025/2
 *
 * Implementa:
 *   - RFC 1928: SOCKS Protocol Version 5
 *   - RFC 1929: Username/Password Authentication for SOCKS V5
 *
 * Arquitectura:
 *   - Single-threaded event loop usando selector.c
 *   - I/O totalmente no bloqueante
 *   - Resolución DNS en thread worker separado
 *
 * Este archivo:
 *   1. Parsea argumentos de línea de comandos
 *   2. Inicializa subsistemas (métricas, usuarios, logging)
 *   3. Crea sockets pasivos (SOCKS y gestión)
 *   4. Registra en el selector y ejecuta el event loop
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "args.h"
#include "selector.h"
#include "socks5nio.h"
#include "mgmt.h"
#include "metrics.h"
#include "users.h"
#include "logger.h"

// Flag global para terminar el servidor limpiamente
static bool done = false;

/**
 * Handler de señales SIGTERM/SIGINT para shutdown limpio.
 */
static void
sigterm_handler(const int signal) {
    printf("\nSignal %d received, cleaning up and exiting...\n", signal);
    done = true;
}

/**
 * Crea un socket TCP pasivo (server socket) configurado para
 * aceptar conexiones en la dirección y puerto especificados.
 *
 * @param addr Dirección IP a escuchar (ej: "0.0.0.0", "127.0.0.1")
 * @param port Puerto a escuchar
 * @param ipv6 true para IPv6, false para IPv4
 * @return file descriptor del socket o -1 en error
 */
static int
create_passive_socket(const char *addr, unsigned short port, bool ipv6) {
    int server;
    
    if (ipv6) {
        struct sockaddr_in6 server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin6_family = AF_INET6;
        server_addr.sin6_port   = htons(port);
        
        if (inet_pton(AF_INET6, addr, &server_addr.sin6_addr) != 1) {
            LOG_ERROR("Invalid IPv6 address: %s", addr);
            return -1;
        }
        
        server = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
        if (server < 0) {
            LOG_ERROR("Unable to create IPv6 socket: %s", strerror(errno));
            return -1;
        }
        
        // Configurar opciones del socket
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
        setsockopt(server, IPPROTO_IPV6, IPV6_V6ONLY, &(int){0}, sizeof(int));
        
        if (bind(server, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            LOG_ERROR("Unable to bind IPv6 socket to [%s]:%d: %s", addr, port, strerror(errno));
            close(server);
            return -1;
        }
    } else {
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port   = htons(port);
        
        if (inet_pton(AF_INET, addr, &server_addr.sin_addr) != 1) {
            LOG_ERROR("Invalid IPv4 address: %s", addr);
            return -1;
        }
        
        server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server < 0) {
            LOG_ERROR("Unable to create IPv4 socket: %s", strerror(errno));
            return -1;
        }
        
        // Configurar opciones del socket
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
        
        if (bind(server, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            LOG_ERROR("Unable to bind IPv4 socket to %s:%d: %s", addr, port, strerror(errno));
            close(server);
            return -1;
        }
    }
    
    // Configurar como no bloqueante
    if (selector_fd_set_nio(server) < 0) {
        LOG_ERROR("Unable to set socket as non-blocking: %s", strerror(errno));
        close(server);
        return -1;
    }
    
    // Escuchar conexiones (backlog grande para soportar 500+ clientes)
    if (listen(server, SOMAXCONN) < 0) {
        LOG_ERROR("Unable to listen on socket: %s", strerror(errno));
        close(server);
        return -1;
    }
    
    return server;
}

/**
 * Determina si una dirección es IPv6
 */
static bool
is_ipv6_address(const char *addr) {
    return strchr(addr, ':') != NULL;
}

int
main(const int argc, char **argv) {
    // Parsear argumentos de línea de comandos
    struct socks5args args;
    parse_args(argc, argv, &args);
    
    // Inicializar subsistemas
    logger_init(LOG_INFO, NULL);  // Log a stderr por defecto
    metrics_init();
    users_init();
    
    // Cargar usuarios de línea de comandos
    for (int i = 0; i < args.nusers; i++) {
        if (!users_add(args.users[i].name, args.users[i].pass)) {
            LOG_ERROR("Failed to add user: %s", args.users[i].name);
        } else {
            LOG_INFO("User added: %s", args.users[i].name);
        }
    }
    
    // Si no hay usuarios, agregar uno por defecto para testing
    if (users_count() == 0) {
        LOG_WARN("No users configured, adding default user admin:admin");
        users_add("admin", "admin");
    }
    
    // Cerrar stdin (no lo necesitamos)
    close(STDIN_FILENO);
    
    // Variables para cleanup
    const char *err_msg   = NULL;
    selector_status ss    = SELECTOR_SUCCESS;
    fd_selector selector  = NULL;
    int socks_server      = -1;
    int mgmt_server       = -1;
    int ret               = 0;
    
    // Crear socket pasivo para SOCKS5
    bool socks_ipv6 = is_ipv6_address(args.socks_addr);
    socks_server = create_passive_socket(args.socks_addr, args.socks_port, socks_ipv6);
    if (socks_server < 0) {
        err_msg = "unable to create SOCKS5 socket";
        ret = 1;
        goto finally;
    }
    LOG_INFO("SOCKS5 server listening on %s%s%s:%d",
             socks_ipv6 ? "[" : "", args.socks_addr, socks_ipv6 ? "]" : "",
             args.socks_port);
    
    // Crear socket pasivo para gestión
    bool mgmt_ipv6 = is_ipv6_address(args.mng_addr);
    mgmt_server = create_passive_socket(args.mng_addr, args.mng_port, mgmt_ipv6);
    if (mgmt_server < 0) {
        err_msg = "unable to create management socket";
        ret = 1;
        goto finally;
    }
    LOG_INFO("Management server listening on %s%s%s:%d",
             mgmt_ipv6 ? "[" : "", args.mng_addr, mgmt_ipv6 ? "]" : "",
             args.mng_port);
    
    // Registrar handlers de señales para shutdown limpio
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT,  sigterm_handler);
    signal(SIGPIPE, SIG_IGN);  // Ignorar SIGPIPE (write en socket cerrado)
    
    // Inicializar el selector
    const struct selector_init selector_conf = {
        .signal = SIGALRM,
        .select_timeout = {
            .tv_sec  = 10,
            .tv_nsec = 0,
        },
    };
    
    if (selector_init(&selector_conf) != SELECTOR_SUCCESS) {
        err_msg = "unable to initialize selector";
        ret = 1;
        goto finally;
    }
    
    selector = selector_new(1024);  // Capacidad inicial para 1024 fds
    if (selector == NULL) {
        err_msg = "unable to create selector";
        ret = 1;
        goto finally;
    }
    
    // Handler para el socket pasivo de SOCKS5
    const struct fd_handler socks5_handler = {
        .handle_read  = socksv5_passive_accept,
        .handle_write = NULL,
        .handle_close = NULL,
    };
    
    ss = selector_register(selector, socks_server, &socks5_handler, OP_READ, NULL);
    if (ss != SELECTOR_SUCCESS) {
        err_msg = "unable to register SOCKS5 socket";
        ret = 1;
        goto finally;
    }
    
    // Handler para el socket pasivo de gestión
    const struct fd_handler mgmt_handler = {
        .handle_read  = mgmt_passive_accept,
        .handle_write = NULL,
        .handle_close = NULL,
    };
    
    ss = selector_register(selector, mgmt_server, &mgmt_handler, OP_READ, NULL);
    if (ss != SELECTOR_SUCCESS) {
        err_msg = "unable to register management socket";
        ret = 1;
        goto finally;
    }
    
    LOG_INFO("Server started successfully. Waiting for connections...");
    
    // ====== EVENT LOOP PRINCIPAL ======
    while (!done) {
        err_msg = NULL;
        ss = selector_select(selector);
        if (ss != SELECTOR_SUCCESS) {
            err_msg = "selector_select failed";
            ret = 1;
            goto finally;
        }
    }
    
    // Llegamos aquí por SIGTERM/SIGINT
    err_msg = "shutting down";
    ret = 0;
    
finally:
    // Cleanup
    if (ss != SELECTOR_SUCCESS) {
        LOG_ERROR("%s: %s", 
                  err_msg ? err_msg : "unknown error",
                  ss == SELECTOR_IO ? strerror(errno) : selector_error(ss));
        ret = 2;
    } else if (err_msg) {
        LOG_INFO("%s", err_msg);
    }
    
    if (selector != NULL) {
        selector_destroy(selector);
    }
    selector_close();
    
    socksv5_pool_destroy();
    mgmt_pool_destroy();
    
    if (socks_server >= 0) {
        close(socks_server);
    }
    if (mgmt_server >= 0) {
        close(mgmt_server);
    }
    
    users_destroy();
    logger_close();
    
    return ret;
}

