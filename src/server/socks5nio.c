/**
 * socks5nio.c - Controla el flujo de un proxy SOCKSv5 (sockets no bloqueantes)
 *
 * Implementa:
 *   - RFC 1928: SOCKS Protocol Version 5
 *   - RFC 1929: Username/Password Authentication for SOCKS V5
 *
 * Arquitectura:
 *   - Máquina de estados finitos usando stm.c
 *   - I/O no bloqueante usando selector.c
 *   - Resolución DNS asíncrona en thread worker
 *
 * Estados de la FSM:
 *   HELLO_READ    -> Lee el mensaje de saludo del cliente
 *   HELLO_WRITE   -> Envía respuesta del saludo
 *   AUTH_READ     -> Lee credenciales (RFC 1929)
 *   AUTH_WRITE    -> Envía resultado de autenticación
 *   REQUEST_READ  -> Lee el request SOCKS5
 *   REQUEST_RESOLVING -> Resolviendo DNS (asíncrono)
 *   REQUEST_CONNECTING -> Conectando al servidor de origen
 *   REQUEST_WRITE -> Envía respuesta del request
 *   COPY          -> Copia datos bidireccional (streaming)
 *   DONE          -> Conexión terminada exitosamente
 *   ERROR         -> Error, cerrar conexión
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "buffer.h"
#include "stm.h"
#include "selector.h"
#include "socks5nio.h"
#include "netutils.h"
#include "users.h"
#include "metrics.h"
#include "logger.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))

// Tamaño de buffers de I/O
#define BUFFER_SIZE 4096

// ============================================================================
// Constantes del protocolo SOCKS5 (RFC 1928)
// ============================================================================

// Versión del protocolo
#define SOCKS_VERSION 0x05

// Métodos de autenticación
#define SOCKS_AUTH_NONE               0x00
#define SOCKS_AUTH_GSSAPI             0x01
#define SOCKS_AUTH_USERNAME_PASSWORD  0x02
#define SOCKS_AUTH_NO_ACCEPTABLE      0xFF

// Comandos
#define SOCKS_CMD_CONNECT       0x01
#define SOCKS_CMD_BIND          0x02
#define SOCKS_CMD_UDP_ASSOCIATE 0x03

// Tipos de dirección
#define SOCKS_ATYP_IPV4   0x01
#define SOCKS_ATYP_DOMAIN 0x03
#define SOCKS_ATYP_IPV6   0x04

// Códigos de respuesta
#define SOCKS_REPLY_SUCCEEDED           0x00
#define SOCKS_REPLY_GENERAL_FAILURE     0x01
#define SOCKS_REPLY_CONN_NOT_ALLOWED    0x02
#define SOCKS_REPLY_NETWORK_UNREACHABLE 0x03
#define SOCKS_REPLY_HOST_UNREACHABLE    0x04
#define SOCKS_REPLY_CONNECTION_REFUSED  0x05
#define SOCKS_REPLY_TTL_EXPIRED         0x06
#define SOCKS_REPLY_CMD_NOT_SUPPORTED   0x07
#define SOCKS_REPLY_ATYP_NOT_SUPPORTED  0x08

// Versión del subnegocio de autenticación (RFC 1929)
#define SOCKS_AUTH_VERSION 0x01
#define SOCKS_AUTH_SUCCESS 0x00
#define SOCKS_AUTH_FAILURE 0x01

// ============================================================================
// Estados de la máquina de estados
// ============================================================================

enum socks5_state {
    HELLO_READ,           // Leyendo saludo del cliente
    HELLO_WRITE,          // Escribiendo respuesta al saludo
    AUTH_READ,            // Leyendo credenciales
    AUTH_WRITE,           // Escribiendo resultado de auth
    REQUEST_READ,         // Leyendo request SOCKS5
    REQUEST_RESOLVING,    // Resolviendo DNS
    REQUEST_CONNECTING,   // Conectando al origen
    REQUEST_WRITE,        // Escribiendo respuesta del request
    COPY,                 // Copiando datos bidireccional
    DONE,                 // Terminado exitosamente
    ERROR,                // Error
};

// ============================================================================
// Estructuras de datos para cada estado
// ============================================================================

// Estado HELLO
struct hello_st {
    buffer *rb, *wb;
    uint8_t methods_count;
    uint8_t methods[256];
    uint8_t selected_method;
};

// Estado AUTH (RFC 1929)
struct auth_st {
    buffer *rb, *wb;
    char username[256];
    char password[256];
    uint8_t ulen;
    uint8_t plen;
    uint8_t status;
};

// Estado REQUEST
struct request_st {
    buffer *rb, *wb;
    uint8_t cmd;
    uint8_t atyp;
    
    // Dirección destino
    union {
        struct in_addr  ipv4;
        struct in6_addr ipv6;
        char fqdn[256];
    } dest_addr;
    uint8_t dest_addr_len;  // Para FQDN
    uint16_t dest_port;
    
    // Para la respuesta
    uint8_t reply;
    struct sockaddr_storage origin_addr;
    socklen_t origin_addr_len;
};

// Estado COPY (streaming bidireccional)
struct copy_st {
    buffer *rb, *wb;
    fd_interest interests;
    
    // Para la otra dirección
    struct copy_st *other;
    
    // Control de flujo
    bool shutdown_read;
    bool shutdown_write;
};

// ============================================================================
// Estructura principal de conexión SOCKS5
// ============================================================================

struct socks5 {
    // File descriptors
    int client_fd;    // Socket del cliente SOCKS
    int origin_fd;    // Socket al servidor de origen
    
    // Información del cliente
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len;
    
    // Información de autenticación
    char username[256];
    
    // Información del destino
    char target_host[256];
    uint16_t target_port;
    
    // Buffers de I/O
    uint8_t raw_buff_read[BUFFER_SIZE];
    uint8_t raw_buff_write[BUFFER_SIZE];
    buffer read_buffer;
    buffer write_buffer;
    
    // Máquina de estados
    struct state_machine stm;
    
    // Estados
    union {
        struct hello_st   hello;
        struct auth_st    auth;
        struct request_st request;
        struct copy_st    copy;
    } client;
    
    struct copy_st origin_copy;
    
    // Resolución DNS
    struct addrinfo *origin_resolution;
    struct addrinfo *origin_resolution_current;
    
    // Métricas de la conexión
    uint64_t bytes_sent;
    uint64_t bytes_recv;
    
    // Pool para reutilización
    struct socks5 *next;
    unsigned references;
};

// ============================================================================
// Pool de conexiones para reutilización
// ============================================================================

static unsigned pool_size = 0;
static const unsigned max_pool = 50;
static struct socks5 *pool = NULL;

// ============================================================================
// Declaraciones forward
// ============================================================================

static void socksv5_read(struct selector_key *key);
static void socksv5_write(struct selector_key *key);
static void socksv5_block(struct selector_key *key);
static void socksv5_close(struct selector_key *key);

static const struct fd_handler socks5_handler = {
    .handle_read   = socksv5_read,
    .handle_write  = socksv5_write,
    .handle_close  = socksv5_close,
    .handle_block  = socksv5_block,
};

// Forward declarations para estados
static void hello_read_init(unsigned state, struct selector_key *key);
static unsigned hello_read(struct selector_key *key);
static unsigned hello_write(struct selector_key *key);

static void auth_read_init(unsigned state, struct selector_key *key);
static unsigned auth_read(struct selector_key *key);
static unsigned auth_write(struct selector_key *key);

static void request_read_init(unsigned state, struct selector_key *key);
static unsigned request_read(struct selector_key *key);
static void request_resolving_init(unsigned state, struct selector_key *key);
static unsigned request_resolving_done(struct selector_key *key);
static void request_connecting_init(unsigned state, struct selector_key *key);
static unsigned request_connecting(struct selector_key *key);
static unsigned request_write(struct selector_key *key);

static void copy_init(unsigned state, struct selector_key *key);
static unsigned copy_read(struct selector_key *key);
static unsigned copy_write(struct selector_key *key);

// ============================================================================
// Definición de la tabla de estados
// ============================================================================

static const struct state_definition client_statbl[] = {
    {
        .state            = HELLO_READ,
        .on_arrival       = hello_read_init,
        .on_read_ready    = hello_read,
    },
    {
        .state            = HELLO_WRITE,
        .on_write_ready   = hello_write,
    },
    {
        .state            = AUTH_READ,
        .on_arrival       = auth_read_init,
        .on_read_ready    = auth_read,
    },
    {
        .state            = AUTH_WRITE,
        .on_write_ready   = auth_write,
    },
    {
        .state            = REQUEST_READ,
        .on_arrival       = request_read_init,
        .on_read_ready    = request_read,
    },
    {
        .state            = REQUEST_RESOLVING,
        .on_arrival       = request_resolving_init,
        .on_block_ready   = request_resolving_done,
    },
    {
        .state            = REQUEST_CONNECTING,
        .on_arrival       = request_connecting_init,
        .on_write_ready   = request_connecting,
    },
    {
        .state            = REQUEST_WRITE,
        .on_write_ready   = request_write,
    },
    {
        .state            = COPY,
        .on_arrival       = copy_init,
        .on_read_ready    = copy_read,
        .on_write_ready   = copy_write,
    },
    {
        .state            = DONE,
    },
    {
        .state            = ERROR,
    },
};

// ============================================================================
// Funciones de gestión de conexiones
// ============================================================================

#define ATTACHMENT(key) ((struct socks5 *)(key)->data)

/**
 * Crea una nueva estructura socks5 (o la obtiene del pool)
 */
static struct socks5 *
socks5_new(int client_fd) {
    struct socks5 *s;
    
    if (pool != NULL) {
        s = pool;
        pool = pool->next;
        pool_size--;
    } else {
        s = malloc(sizeof(*s));
        if (s == NULL) {
            return NULL;
        }
    }
    
    memset(s, 0, sizeof(*s));
    
    s->client_fd = client_fd;
    s->origin_fd = -1;
    s->references = 1;
    
    // Inicializar buffers
    buffer_init(&s->read_buffer, BUFFER_SIZE, s->raw_buff_read);
    buffer_init(&s->write_buffer, BUFFER_SIZE, s->raw_buff_write);
    
    // Inicializar máquina de estados
    s->stm.initial   = HELLO_READ;
    s->stm.max_state = ERROR;
    s->stm.states    = client_statbl;
    stm_init(&s->stm);
    
    metrics_connection_opened();
    
    return s;
}

/**
 * Destruye una estructura socks5
 */
static void
socks5_destroy_(struct socks5 *s) {
    if (s->origin_resolution != NULL) {
        freeaddrinfo(s->origin_resolution);
        s->origin_resolution = NULL;
    }
    free(s);
}

/**
 * Destruye o devuelve al pool una estructura socks5
 */
static void
socks5_destroy(struct socks5 *s) {
    if (s == NULL) {
        return;
    }
    
    if (s->references == 1) {
        // Registrar acceso antes de destruir
        log_access(s->username[0] ? s->username : NULL,
                   (struct sockaddr *)&s->client_addr,
                   s->target_host[0] ? s->target_host : NULL,
                   s->target_port,
                   stm_state(&s->stm) == DONE ? "OK" : "ERROR",
                   s->bytes_sent,
                   s->bytes_recv);
        
        metrics_connection_closed();
        
        if (pool_size < max_pool) {
            s->next = pool;
            pool = s;
            pool_size++;
        } else {
            socks5_destroy_(s);
        }
    } else {
        s->references--;
    }
}

void
socksv5_pool_destroy(void) {
    struct socks5 *next, *s;
    for (s = pool; s != NULL; s = next) {
        next = s->next;
        free(s);
    }
    pool = NULL;
    pool_size = 0;
}

// ============================================================================
// Accept de nuevas conexiones
// ============================================================================

void
socksv5_passive_accept(struct selector_key *key) {
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    struct socks5 *state = NULL;
    
    const int client = accept(key->fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client == -1) {
        goto fail;
    }
    
    if (selector_fd_set_nio(client) == -1) {
        goto fail;
    }
    
    state = socks5_new(client);
    if (state == NULL) {
        goto fail;
    }
    
    memcpy(&state->client_addr, &client_addr, client_addr_len);
    state->client_addr_len = client_addr_len;
    
    char client_str[SOCKADDR_TO_HUMAN_MIN];
    sockaddr_to_human(client_str, sizeof(client_str), (struct sockaddr *)&client_addr);
    LOG_DEBUG("New connection from %s", client_str);
    
    if (SELECTOR_SUCCESS != selector_register(key->s, client, &socks5_handler,
                                               OP_READ, state)) {
        goto fail;
    }
    return;
    
fail:
    if (client != -1) {
        close(client);
    }
    socks5_destroy(state);
}

// ============================================================================
// Estado HELLO (RFC 1928 Section 3)
// ============================================================================

static void
hello_read_init(unsigned state, struct selector_key *key) {
    (void)state;
    struct socks5 *s = ATTACHMENT(key);
    struct hello_st *d = &s->client.hello;
    
    d->rb = &s->read_buffer;
    d->wb = &s->write_buffer;
    d->methods_count = 0;
    d->selected_method = SOCKS_AUTH_NO_ACCEPTABLE;
}

/**
 * Lee el mensaje HELLO del cliente:
 *   +----+----------+----------+
 *   |VER | NMETHODS | METHODS  |
 *   +----+----------+----------+
 *   | 1  |    1     | 1 to 255 |
 *   +----+----------+----------+
 */
static unsigned
hello_read(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    struct hello_st *d = &s->client.hello;
    uint8_t *ptr;
    size_t count;
    ssize_t n;
    
    ptr = buffer_write_ptr(d->rb, &count);
    n = recv(key->fd, ptr, count, 0);
    
    if (n <= 0) {
        return ERROR;
    }
    
    buffer_write_adv(d->rb, n);
    
    // Intentar parsear el mensaje
    if (!buffer_can_read(d->rb)) {
        return HELLO_READ;
    }
    
    // Leer versión
    uint8_t version = buffer_read(d->rb);
    if (version != SOCKS_VERSION) {
        LOG_WARN("Invalid SOCKS version: %d", version);
        return ERROR;
    }
    
    // Leer número de métodos
    if (!buffer_can_read(d->rb)) {
        return HELLO_READ;
    }
    d->methods_count = buffer_read(d->rb);
    
    // Leer métodos
    size_t available;
    buffer_read_ptr(d->rb, &available);
    if (available < d->methods_count) {
        // Necesitamos más datos
        return HELLO_READ;
    }
    
    // Buscar método de autenticación soportado
    // Requerimos USERNAME/PASSWORD según RFC 1929
    d->selected_method = SOCKS_AUTH_NO_ACCEPTABLE;
    for (uint8_t i = 0; i < d->methods_count; i++) {
        uint8_t method = buffer_read(d->rb);
        if (method == SOCKS_AUTH_USERNAME_PASSWORD) {
            d->selected_method = SOCKS_AUTH_USERNAME_PASSWORD;
        }
    }
    
    // Preparar respuesta
    buffer_reset(d->wb);
    buffer_write(d->wb, SOCKS_VERSION);
    buffer_write(d->wb, d->selected_method);
    
    if (SELECTOR_SUCCESS != selector_set_interest_key(key, OP_WRITE)) {
        return ERROR;
    }
    
    return HELLO_WRITE;
}

/**
 * Escribe respuesta HELLO:
 *   +----+--------+
 *   |VER | METHOD |
 *   +----+--------+
 *   | 1  |   1    |
 *   +----+--------+
 */
static unsigned
hello_write(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    struct hello_st *d = &s->client.hello;
    
    uint8_t *ptr;
    size_t count;
    ssize_t n;
    
    ptr = buffer_read_ptr(d->wb, &count);
    n = send(key->fd, ptr, count, MSG_NOSIGNAL);
    
    if (n <= 0) {
        return ERROR;
    }
    
    buffer_read_adv(d->wb, n);
    
    if (buffer_can_read(d->wb)) {
        return HELLO_WRITE;
    }
    
    // Verificar si tenemos método aceptable
    if (d->selected_method == SOCKS_AUTH_NO_ACCEPTABLE) {
        LOG_WARN("No acceptable auth method");
        return ERROR;
    }
    
    if (SELECTOR_SUCCESS != selector_set_interest_key(key, OP_READ)) {
        return ERROR;
    }
    
    return AUTH_READ;
}

// ============================================================================
// Estado AUTH (RFC 1929)
// ============================================================================

static void
auth_read_init(unsigned state, struct selector_key *key) {
    (void)state;
    struct socks5 *s = ATTACHMENT(key);
    struct auth_st *d = &s->client.auth;
    
    d->rb = &s->read_buffer;
    d->wb = &s->write_buffer;
    buffer_reset(d->rb);
    d->ulen = 0;
    d->plen = 0;
    d->status = SOCKS_AUTH_FAILURE;
}

/**
 * Lee autenticación username/password:
 *   +----+------+----------+------+----------+
 *   |VER | ULEN |  UNAME   | PLEN |  PASSWD  |
 *   +----+------+----------+------+----------+
 *   | 1  |  1   | 1 to 255 |  1   | 1 to 255 |
 *   +----+------+----------+------+----------+
 */
static unsigned
auth_read(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    struct auth_st *d = &s->client.auth;
    uint8_t *ptr;
    size_t count;
    ssize_t n;
    
    ptr = buffer_write_ptr(d->rb, &count);
    n = recv(key->fd, ptr, count, 0);
    
    if (n <= 0) {
        return ERROR;
    }
    
    buffer_write_adv(d->rb, n);
    
    // Parsear mensaje
    size_t available;
    buffer_read_ptr(d->rb, &available);
    
    if (available < 2) {
        return AUTH_READ;
    }
    
    // Guardar posición actual
    uint8_t *read_start = d->rb->read;
    
    uint8_t version = buffer_read(d->rb);
    if (version != SOCKS_AUTH_VERSION) {
        LOG_WARN("Invalid auth version: %d", version);
        return ERROR;
    }
    
    d->ulen = buffer_read(d->rb);
    
    // Verificar que tenemos todo el username
    buffer_read_ptr(d->rb, &available);
    if (available < (size_t)(d->ulen + 1)) {  // +1 para PLEN
        d->rb->read = read_start;  // Restaurar
        return AUTH_READ;
    }
    
    // Leer username
    for (uint8_t i = 0; i < d->ulen; i++) {
        d->username[i] = buffer_read(d->rb);
    }
    d->username[d->ulen] = '\0';
    
    d->plen = buffer_read(d->rb);
    
    // Verificar que tenemos todo el password
    buffer_read_ptr(d->rb, &available);
    if (available < d->plen) {
        d->rb->read = read_start;  // Restaurar
        return AUTH_READ;
    }
    
    // Leer password
    for (uint8_t i = 0; i < d->plen; i++) {
        d->password[i] = buffer_read(d->rb);
    }
    d->password[d->plen] = '\0';
    
    // Verificar credenciales
    if (users_verify(d->username, d->password)) {
        d->status = SOCKS_AUTH_SUCCESS;
        strncpy(s->username, d->username, sizeof(s->username) - 1);
        LOG_DEBUG("User %s authenticated successfully", d->username);
    } else {
        d->status = SOCKS_AUTH_FAILURE;
        LOG_WARN("Authentication failed for user: %s", d->username);
        metrics_connection_failed();
    }
    
    // Limpiar password de memoria
    memset(d->password, 0, sizeof(d->password));
    
    // Preparar respuesta
    buffer_reset(d->wb);
    buffer_write(d->wb, SOCKS_AUTH_VERSION);
    buffer_write(d->wb, d->status);
    
    if (SELECTOR_SUCCESS != selector_set_interest_key(key, OP_WRITE)) {
        return ERROR;
    }
    
    return AUTH_WRITE;
}

/**
 * Escribe respuesta de autenticación:
 *   +----+--------+
 *   |VER | STATUS |
 *   +----+--------+
 *   | 1  |   1    |
 *   +----+--------+
 */
static unsigned
auth_write(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    struct auth_st *d = &s->client.auth;
    
    uint8_t *ptr;
    size_t count;
    ssize_t n;
    
    ptr = buffer_read_ptr(d->wb, &count);
    n = send(key->fd, ptr, count, MSG_NOSIGNAL);
    
    if (n <= 0) {
        return ERROR;
    }
    
    buffer_read_adv(d->wb, n);
    
    if (buffer_can_read(d->wb)) {
        return AUTH_WRITE;
    }
    
    // Si falló la auth, cerrar
    if (d->status != SOCKS_AUTH_SUCCESS) {
        return ERROR;
    }
    
    if (SELECTOR_SUCCESS != selector_set_interest_key(key, OP_READ)) {
        return ERROR;
    }
    
    return REQUEST_READ;
}

// ============================================================================
// Estado REQUEST (RFC 1928 Section 4)
// ============================================================================

static void
request_read_init(unsigned state, struct selector_key *key) {
    (void)state;
    struct socks5 *s = ATTACHMENT(key);
    struct request_st *d = &s->client.request;
    
    d->rb = &s->read_buffer;
    d->wb = &s->write_buffer;
    buffer_reset(d->rb);
    d->reply = SOCKS_REPLY_SUCCEEDED;
}

/**
 * Lee request SOCKS5:
 *   +----+-----+-------+------+----------+----------+
 *   |VER | CMD |  RSV  | ATYP | DST.ADDR | DST.PORT |
 *   +----+-----+-------+------+----------+----------+
 *   | 1  |  1  | X'00' |  1   | Variable |    2     |
 *   +----+-----+-------+------+----------+----------+
 */
static unsigned
request_read(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    struct request_st *d = &s->client.request;
    uint8_t *ptr;
    size_t count;
    ssize_t n;
    
    ptr = buffer_write_ptr(d->rb, &count);
    n = recv(key->fd, ptr, count, 0);
    
    if (n <= 0) {
        return ERROR;
    }
    
    buffer_write_adv(d->rb, n);
    
    // Parsear request
    size_t available;
    buffer_read_ptr(d->rb, &available);
    
    if (available < 4) {
        return REQUEST_READ;
    }
    
    uint8_t *read_start = d->rb->read;
    
    uint8_t version = buffer_read(d->rb);
    if (version != SOCKS_VERSION) {
        d->reply = SOCKS_REPLY_GENERAL_FAILURE;
        goto prepare_response;
    }
    
    d->cmd = buffer_read(d->rb);
    buffer_read(d->rb);  // RSV
    d->atyp = buffer_read(d->rb);
    
    // Solo soportamos CONNECT
    if (d->cmd != SOCKS_CMD_CONNECT) {
        LOG_WARN("Unsupported command: %d", d->cmd);
        d->reply = SOCKS_REPLY_CMD_NOT_SUPPORTED;
        goto prepare_response;
    }
    
    // Leer dirección según tipo
    buffer_read_ptr(d->rb, &available);
    
    switch (d->atyp) {
        case SOCKS_ATYP_IPV4:
            if (available < 4 + 2) {
                d->rb->read = read_start;
                return REQUEST_READ;
            }
            for (int i = 0; i < 4; i++) {
                ((uint8_t *)&d->dest_addr.ipv4)[i] = buffer_read(d->rb);
            }
            inet_ntop(AF_INET, &d->dest_addr.ipv4, s->target_host, sizeof(s->target_host));
            break;
            
        case SOCKS_ATYP_IPV6:
            if (available < 16 + 2) {
                d->rb->read = read_start;
                return REQUEST_READ;
            }
            for (int i = 0; i < 16; i++) {
                d->dest_addr.ipv6.s6_addr[i] = buffer_read(d->rb);
            }
            inet_ntop(AF_INET6, &d->dest_addr.ipv6, s->target_host, sizeof(s->target_host));
            break;
            
        case SOCKS_ATYP_DOMAIN:
            d->dest_addr_len = buffer_read(d->rb);
            buffer_read_ptr(d->rb, &available);
            if (available < (size_t)(d->dest_addr_len + 2)) {
                d->rb->read = read_start;
                return REQUEST_READ;
            }
            for (uint8_t i = 0; i < d->dest_addr_len; i++) {
                d->dest_addr.fqdn[i] = buffer_read(d->rb);
            }
            d->dest_addr.fqdn[d->dest_addr_len] = '\0';
            strncpy(s->target_host, d->dest_addr.fqdn, sizeof(s->target_host) - 1);
            break;
            
        default:
            LOG_WARN("Unsupported address type: %d", d->atyp);
            d->reply = SOCKS_REPLY_ATYP_NOT_SUPPORTED;
            goto prepare_response;
    }
    
    // Leer puerto (big endian)
    d->dest_port = buffer_read(d->rb) << 8;
    d->dest_port |= buffer_read(d->rb);
    s->target_port = d->dest_port;
    
    LOG_DEBUG("CONNECT request to %s:%d", s->target_host, d->dest_port);
    
    // Si es FQDN, necesitamos resolver DNS
    if (d->atyp == SOCKS_ATYP_DOMAIN) {
        selector_set_interest_key(key, OP_NOOP);
        return REQUEST_RESOLVING;
    }
    
    // Para IPv4/IPv6, conectar directamente
    selector_set_interest_key(key, OP_WRITE);
    return REQUEST_CONNECTING;
    
prepare_response:
    // Error, preparar respuesta negativa
    selector_set_interest_key(key, OP_WRITE);
    return REQUEST_WRITE;
}

// ============================================================================
// Resolución DNS asíncrona
// ============================================================================

struct resolve_args {
    fd_selector selector;
    int client_fd;
    char host[256];
    uint16_t port;
    struct socks5 *s;  // Puntero a la estructura para guardar el resultado
};

static void *
resolve_thread(void *arg) {
    struct resolve_args *args = arg;
    
    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };
    
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", args->port);
    
    struct addrinfo *result = NULL;
    int status = getaddrinfo(args->host, port_str, &hints, &result);
    if (status != 0) {
        LOG_WARN("DNS resolution failed for %s: %s", args->host, gai_strerror(status));
        result = NULL;
    }
    
    // Guardar resultado en la estructura socks5 (acceso thread-safe por diseño:
    // el selector no procesa este fd mientras está en estado BLOCK)
    args->s->origin_resolution = result;
    args->s->origin_resolution_current = result;
    
    // Notificar al selector que terminamos
    selector_notify_block(args->selector, args->client_fd);
    
    free(args);
    return NULL;
}

static void
request_resolving_init(unsigned state, struct selector_key *key) {
    (void)state;
    struct socks5 *s = ATTACHMENT(key);
    struct request_st *d = &s->client.request;
    
    // Limpiar resolución anterior si existe
    if (s->origin_resolution != NULL) {
        freeaddrinfo(s->origin_resolution);
        s->origin_resolution = NULL;
        s->origin_resolution_current = NULL;
    }
    
    struct resolve_args *args = malloc(sizeof(*args));
    if (args == NULL) {
        d->reply = SOCKS_REPLY_GENERAL_FAILURE;
        return;
    }
    
    args->selector = key->s;
    args->client_fd = key->fd;
    strncpy(args->host, d->dest_addr.fqdn, sizeof(args->host) - 1);
    args->host[sizeof(args->host) - 1] = '\0';
    args->port = d->dest_port;
    args->s = s;  // Puntero para que el thread guarde el resultado
    
    pthread_t tid;
    if (pthread_create(&tid, NULL, resolve_thread, args) != 0) {
        free(args);
        d->reply = SOCKS_REPLY_GENERAL_FAILURE;
        return;
    }
    pthread_detach(tid);
    
    LOG_DEBUG("DNS resolution started for %s in separate thread (non-blocking)", args->host);
}

static unsigned
request_resolving_done(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    (void)s;  // Evitar warning de variable no usada
    
    // La resolución se guarda en s->origin_resolution por el thread
    // (simplificación: asumimos que llegó)
    
    // En una implementación completa, guardaríamos el resultado
    // Por ahora, conectamos directamente
    
    selector_set_interest_key(key, OP_WRITE);
    return REQUEST_CONNECTING;
}

// ============================================================================
// Conexión al servidor de origen
// ============================================================================

/**
 * Intenta conectar a una dirección del listado de resolución DNS.
 * Si falla, intenta con la siguiente (robustez requerida por consigna punto 4).
 * 
 * @return true si se inició conexión, false si hay que preparar respuesta de error
 */
static bool
try_connect_to_origin(struct socks5 *s, struct selector_key *key) {
    struct request_st *d = &s->client.request;
    
    struct addrinfo *current = s->origin_resolution_current;
    if (current == NULL) {
        d->reply = SOCKS_REPLY_HOST_UNREACHABLE;
        return false;
    }
    
    // Intentar con cada dirección hasta que una funcione
    while (current != NULL) {
        int origin_fd = socket(current->ai_family, SOCK_STREAM, IPPROTO_TCP);
        if (origin_fd < 0) {
            current = current->ai_next;
            continue;
        }
        
        if (selector_fd_set_nio(origin_fd) < 0) {
            close(origin_fd);
            current = current->ai_next;
            continue;
        }
        
        // Iniciar conexión no bloqueante
        int ret = connect(origin_fd, current->ai_addr, current->ai_addrlen);
        if (ret < 0 && errno != EINPROGRESS) {
            LOG_DEBUG("Connect to address failed: %s, trying next...", strerror(errno));
            close(origin_fd);
            current = current->ai_next;
            continue;
        }
        
        // Conexión iniciada exitosamente
        s->origin_fd = origin_fd;
        memcpy(&d->origin_addr, current->ai_addr, current->ai_addrlen);
        d->origin_addr_len = current->ai_addrlen;
        s->origin_resolution_current = current->ai_next;  // Para reintentar si falla
        
        // Registrar el fd del origen para escribir (esperar conexión)
        s->references++;
        selector_register(key->s, origin_fd, &socks5_handler, OP_WRITE, s);
        
        return true;
    }
    
    // No se pudo conectar a ninguna dirección
    d->reply = SOCKS_REPLY_HOST_UNREACHABLE;
    return false;
}

/**
 * Inicia la conexión al servidor de origen.
 * 
 * Dos casos:
 * 1. FQDN: Viene desde REQUEST_RESOLVING, ya tiene s->origin_resolution del thread
 * 2. IPv4/IPv6: Conexión directa sin DNS lookup (NO BLOQUEANTE)
 */
static void
request_connecting_init(unsigned state, struct selector_key *key) {
    (void)state;
    struct socks5 *s = ATTACHMENT(key);
    struct request_st *d = &s->client.request;
    
    // CASO 1: Viene de RESOLVING (FQDN) - ya tiene resolución del thread
    if (s->origin_resolution != NULL) {
        LOG_DEBUG("Connecting to %s:%d using resolved addresses (from DNS thread)", 
                  s->target_host, d->dest_port);
        
        // Usar try_connect_to_origin que itera sobre las direcciones resueltas
        if (!try_connect_to_origin(s, key)) {
            // Error preparado, ir a escribir respuesta
            selector_set_interest(key->s, s->client_fd, OP_WRITE);
            return;
        }
        
        // Quitar interés del cliente mientras conectamos
        selector_set_interest(key->s, s->client_fd, OP_NOOP);
        return;
    }
    
    // CASO 2: IPv4/IPv6 directo - crear sockaddr sin getaddrinfo (NO BLOQUEANTE)
    int origin_fd = -1;
    
    if (d->atyp == SOCKS_ATYP_IPV4) {
        // Crear socket IPv4
        origin_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (origin_fd < 0) {
            LOG_WARN("Failed to create IPv4 socket: %s", strerror(errno));
            d->reply = SOCKS_REPLY_GENERAL_FAILURE;
            selector_set_interest(key->s, s->client_fd, OP_WRITE);
            return;
        }
        
        // Setear no bloqueante ANTES de connect
        if (selector_fd_set_nio(origin_fd) < 0) {
            LOG_WARN("Failed to set socket non-blocking: %s", strerror(errno));
            close(origin_fd);
            d->reply = SOCKS_REPLY_GENERAL_FAILURE;
            selector_set_interest(key->s, s->client_fd, OP_WRITE);
            return;
        }
        
        // Construir sockaddr_in directamente (NO usa getaddrinfo = NO BLOQUEANTE)
        struct sockaddr_in addr4;
        memset(&addr4, 0, sizeof(addr4));
        addr4.sin_family = AF_INET;
        addr4.sin_port = htons(d->dest_port);
        memcpy(&addr4.sin_addr, &d->dest_addr.ipv4, sizeof(addr4.sin_addr));
        
        // Iniciar conexión no bloqueante
        int ret = connect(origin_fd, (struct sockaddr *)&addr4, sizeof(addr4));
        if (ret < 0 && errno != EINPROGRESS) {
            LOG_DEBUG("Connect to IPv4 failed: %s", strerror(errno));
            close(origin_fd);
            d->reply = SOCKS_REPLY_HOST_UNREACHABLE;
            selector_set_interest(key->s, s->client_fd, OP_WRITE);
            return;
        }
        
        // Guardar dirección para la respuesta
        memcpy(&d->origin_addr, &addr4, sizeof(addr4));
        d->origin_addr_len = sizeof(addr4);
        
    } else if (d->atyp == SOCKS_ATYP_IPV6) {
        // Crear socket IPv6
        origin_fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
        if (origin_fd < 0) {
            LOG_WARN("Failed to create IPv6 socket: %s", strerror(errno));
            d->reply = SOCKS_REPLY_GENERAL_FAILURE;
            selector_set_interest(key->s, s->client_fd, OP_WRITE);
            return;
        }
        
        // Setear no bloqueante ANTES de connect
        if (selector_fd_set_nio(origin_fd) < 0) {
            LOG_WARN("Failed to set socket non-blocking: %s", strerror(errno));
            close(origin_fd);
            d->reply = SOCKS_REPLY_GENERAL_FAILURE;
            selector_set_interest(key->s, s->client_fd, OP_WRITE);
            return;
        }
        
        // Construir sockaddr_in6 directamente (NO usa getaddrinfo = NO BLOQUEANTE)
        struct sockaddr_in6 addr6;
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons(d->dest_port);
        memcpy(&addr6.sin6_addr, &d->dest_addr.ipv6, sizeof(addr6.sin6_addr));
        
        // Iniciar conexión no bloqueante
        int ret = connect(origin_fd, (struct sockaddr *)&addr6, sizeof(addr6));
        if (ret < 0 && errno != EINPROGRESS) {
            LOG_DEBUG("Connect to IPv6 failed: %s", strerror(errno));
            close(origin_fd);
            d->reply = SOCKS_REPLY_HOST_UNREACHABLE;
            selector_set_interest(key->s, s->client_fd, OP_WRITE);
            return;
        }
        
        // Guardar dirección para la respuesta
        memcpy(&d->origin_addr, &addr6, sizeof(addr6));
        d->origin_addr_len = sizeof(addr6);
        
    } else {
        // FQDN sin resolución previa = error (debería haber ido por RESOLVING)
        LOG_ERROR("FQDN without DNS resolution - this should not happen");
        d->reply = SOCKS_REPLY_GENERAL_FAILURE;
        selector_set_interest(key->s, s->client_fd, OP_WRITE);
        return;
    }
    
    LOG_DEBUG("Connecting to %s:%d (direct IP, non-blocking)", s->target_host, d->dest_port);
    
    // Conexión iniciada exitosamente
    s->origin_fd = origin_fd;
    s->origin_resolution_current = NULL;  // No hay más direcciones para reintentar
    
    // Registrar el fd del origen para escribir (esperar conexión)
    s->references++;
    if (selector_register(key->s, origin_fd, &socks5_handler, OP_WRITE, s) != SELECTOR_SUCCESS) {
        LOG_ERROR("Failed to register origin socket");
        close(origin_fd);
        s->origin_fd = -1;
        s->references--;
        d->reply = SOCKS_REPLY_GENERAL_FAILURE;
        selector_set_interest(key->s, s->client_fd, OP_WRITE);
        return;
    }
    
    // Quitar interés del cliente mientras conectamos
    selector_set_interest(key->s, s->client_fd, OP_NOOP);
}

static unsigned
request_connecting(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    struct request_st *d = &s->client.request;
    
    // Verificar si la conexión fue exitosa
    int error;
    socklen_t len = sizeof(error);
    
    if (getsockopt(s->origin_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
        LOG_DEBUG("Connection to origin failed: %s", strerror(error));
        
        // Desregistrar el fd fallido
        selector_unregister_fd(key->s, s->origin_fd);
        close(s->origin_fd);
        s->origin_fd = -1;
        
        // ROBUSTEZ: Intentar con la siguiente dirección IP si hay más
        // (Requerimiento funcional 4 de la consigna)
        if (s->origin_resolution_current != NULL) {
            LOG_DEBUG("Trying next address in resolution list...");
            if (try_connect_to_origin(s, key)) {
                // Conexión iniciada con otra IP, seguir esperando
                return REQUEST_CONNECTING;
            }
        }
        
        // No hay más direcciones, reportar error
        d->reply = SOCKS_REPLY_CONNECTION_REFUSED;
        selector_set_interest(key->s, s->client_fd, OP_WRITE);
        return REQUEST_WRITE;
    }
    
    LOG_DEBUG("Connected to origin successfully");
    d->reply = SOCKS_REPLY_SUCCEEDED;
    metrics_connection_success();
    
    // Preparar respuesta exitosa
    selector_set_interest(key->s, s->client_fd, OP_WRITE);
    selector_set_interest(key->s, s->origin_fd, OP_NOOP);
    
    return REQUEST_WRITE;
}

/**
 * Escribe respuesta del request:
 *   +----+-----+-------+------+----------+----------+
 *   |VER | REP |  RSV  | ATYP | BND.ADDR | BND.PORT |
 *   +----+-----+-------+------+----------+----------+
 *   | 1  |  1  | X'00' |  1   | Variable |    2     |
 *   +----+-----+-------+------+----------+----------+
 */
static unsigned
request_write(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    struct request_st *d = &s->client.request;
    
    // Preparar respuesta si no está lista
    if (!buffer_can_read(d->wb)) {
        buffer_reset(d->wb);
        buffer_write(d->wb, SOCKS_VERSION);
        buffer_write(d->wb, d->reply);
        buffer_write(d->wb, 0x00);  // RSV
        
        // BND.ADDR y BND.PORT
        // Usamos 0.0.0.0:0 por simplicidad
        buffer_write(d->wb, SOCKS_ATYP_IPV4);
        buffer_write(d->wb, 0x00);
        buffer_write(d->wb, 0x00);
        buffer_write(d->wb, 0x00);
        buffer_write(d->wb, 0x00);
        buffer_write(d->wb, 0x00);
        buffer_write(d->wb, 0x00);
    }
    
    uint8_t *ptr;
    size_t count;
    ssize_t n;
    
    ptr = buffer_read_ptr(d->wb, &count);
    n = send(s->client_fd, ptr, count, MSG_NOSIGNAL);
    
    if (n <= 0) {
        return ERROR;
    }
    
    buffer_read_adv(d->wb, n);
    
    if (buffer_can_read(d->wb)) {
        return REQUEST_WRITE;
    }
    
    // Si hubo error, terminar
    if (d->reply != SOCKS_REPLY_SUCCEEDED) {
        return ERROR;
    }
    
    // Pasar a modo COPY
    return COPY;
}

// ============================================================================
// Estado COPY (streaming bidireccional)
// ============================================================================

static void
copy_init(unsigned state, struct selector_key *key) {
    (void)state;
    struct socks5 *s = ATTACHMENT(key);
    
    // Reiniciar buffers
    buffer_reset(&s->read_buffer);
    buffer_reset(&s->write_buffer);
    
    // Configurar estructuras de copy
    struct copy_st *client_copy = &s->client.copy;
    struct copy_st *origin_copy = &s->origin_copy;
    
    client_copy->rb = &s->read_buffer;
    client_copy->wb = &s->write_buffer;
    client_copy->other = origin_copy;
    client_copy->interests = OP_READ;
    client_copy->shutdown_read = false;
    client_copy->shutdown_write = false;
    
    origin_copy->rb = &s->write_buffer;  // Invertido
    origin_copy->wb = &s->read_buffer;
    origin_copy->other = client_copy;
    origin_copy->interests = OP_READ;
    origin_copy->shutdown_read = false;
    origin_copy->shutdown_write = false;
    
    // Ambos lados listos para leer
    selector_set_interest(key->s, s->client_fd, OP_READ);
    selector_set_interest(key->s, s->origin_fd, OP_READ);
}

/**
 * Calcula los intereses basado en el estado de los buffers
 */
static fd_interest
copy_compute_interests(struct socks5 *s, int fd) {
    fd_interest ret = OP_NOOP;
    
    bool is_client = (fd == s->client_fd);
    struct copy_st *copy = is_client ? &s->client.copy : &s->origin_copy;
    
    // Podemos leer si el buffer de escritura del otro lado tiene espacio
    if (!copy->shutdown_read && buffer_can_write(copy->other->wb)) {
        ret |= OP_READ;
    }
    
    // Podemos escribir si nuestro buffer de escritura tiene datos
    if (buffer_can_read(copy->wb)) {
        ret |= OP_WRITE;
    }
    
    return ret;
}

static unsigned
copy_read(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    
    bool is_client = (key->fd == s->client_fd);
    struct copy_st *copy = is_client ? &s->client.copy : &s->origin_copy;
    
    uint8_t *ptr;
    size_t count;
    ssize_t n;
    
    // Leer hacia el buffer de escritura del otro lado
    ptr = buffer_write_ptr(copy->other->wb, &count);
    n = recv(key->fd, ptr, count, 0);
    
    if (n <= 0) {
        if (n == 0 || errno != EAGAIN) {
            copy->shutdown_read = true;
            shutdown(key->fd, SHUT_RD);
            copy->other->shutdown_write = true;
        }
    } else {
        buffer_write_adv(copy->other->wb, n);
        
        // Actualizar métricas
        if (is_client) {
            s->bytes_recv += n;
            metrics_add_bytes_received(n);
        } else {
            s->bytes_sent += n;
            metrics_add_bytes_sent(n);
        }
    }
    
    // Actualizar intereses
    selector_set_interest(key->s, s->client_fd, copy_compute_interests(s, s->client_fd));
    if (s->origin_fd >= 0) {
        selector_set_interest(key->s, s->origin_fd, copy_compute_interests(s, s->origin_fd));
    }
    
    // Verificar si terminamos
    if (s->client.copy.shutdown_read && s->origin_copy.shutdown_read &&
        !buffer_can_read(&s->read_buffer) && !buffer_can_read(&s->write_buffer)) {
        return DONE;
    }
    
    return COPY;
}

static unsigned
copy_write(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    
    bool is_client = (key->fd == s->client_fd);
    struct copy_st *copy = is_client ? &s->client.copy : &s->origin_copy;
    
    uint8_t *ptr;
    size_t count;
    ssize_t n;
    
    ptr = buffer_read_ptr(copy->wb, &count);
    n = send(key->fd, ptr, count, MSG_NOSIGNAL);
    
    if (n <= 0) {
        if (errno != EAGAIN) {
            copy->shutdown_write = true;
            copy->other->shutdown_read = true;
        }
    } else {
        buffer_read_adv(copy->wb, n);
    }
    
    // Actualizar intereses
    selector_set_interest(key->s, s->client_fd, copy_compute_interests(s, s->client_fd));
    if (s->origin_fd >= 0) {
        selector_set_interest(key->s, s->origin_fd, copy_compute_interests(s, s->origin_fd));
    }
    
    // Verificar si terminamos
    if (s->client.copy.shutdown_read && s->origin_copy.shutdown_read &&
        !buffer_can_read(&s->read_buffer) && !buffer_can_read(&s->write_buffer)) {
        return DONE;
    }
    
    return COPY;
}

// ============================================================================
// Handlers del selector
// ============================================================================

static void
socksv5_done(struct selector_key *key);

static void
socksv5_read(struct selector_key *key) {
    struct state_machine *stm = &ATTACHMENT(key)->stm;
    const enum socks5_state st = stm_handler_read(stm, key);
    
    if (ERROR == st || DONE == st) {
        socksv5_done(key);
    }
}

static void
socksv5_write(struct selector_key *key) {
    struct state_machine *stm = &ATTACHMENT(key)->stm;
    const enum socks5_state st = stm_handler_write(stm, key);
    
    if (ERROR == st || DONE == st) {
        socksv5_done(key);
    }
}

static void
socksv5_block(struct selector_key *key) {
    struct state_machine *stm = &ATTACHMENT(key)->stm;
    const enum socks5_state st = stm_handler_block(stm, key);
    
    if (ERROR == st || DONE == st) {
        socksv5_done(key);
    }
}

static void
socksv5_close(struct selector_key *key) {
    socks5_destroy(ATTACHMENT(key));
}

static void
socksv5_done(struct selector_key *key) {
    struct socks5 *s = ATTACHMENT(key);
    
    if (s->client_fd >= 0) {
        selector_unregister_fd(key->s, s->client_fd);
        close(s->client_fd);
        s->client_fd = -1;
    }
    
    if (s->origin_fd >= 0) {
        selector_unregister_fd(key->s, s->origin_fd);
        close(s->origin_fd);
        s->origin_fd = -1;
    }
}

