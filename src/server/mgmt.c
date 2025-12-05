/**
 * mgmt.c - Servidor de gestión/monitoreo
 *
 * Implementa un protocolo de gestión basado en texto para:
 *   - Consultar métricas
 *   - Gestionar usuarios
 *   - Ver estado del servidor
 *
 * Protocolo:
 *   - Comandos tipo texto terminados en \r\n
 *   - Respuestas: +OK <mensaje>\r\n o -ERR <mensaje>\r\n
 *
 * Comandos soportados:
 *   AUTH <user> <pass>    - Autenticación del admin
 *   STATS                 - Muestra estadísticas
 *   USERS                 - Lista usuarios
 *   ADDUSER <user> <pass> - Agrega usuario
 *   DELUSER <user>        - Elimina usuario
 *   HELP                  - Muestra ayuda
 *   QUIT                  - Cierra conexión
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  // strcasecmp
#include <errno.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "buffer.h"
#include "selector.h"
#include "stm.h"
#include "mgmt.h"
#include "metrics.h"
#include "users.h"
#include "logger.h"
#include "netutils.h"

#define BUFFER_SIZE 4096

// Credenciales de administrador (hardcodeadas para simplicidad)
// En producción deberían estar en un archivo de configuración
#define ADMIN_USER "admin"
#define ADMIN_PASS "admin123"

// Estados del protocolo de gestión
enum mgmt_state {
    MGMT_AUTH,      // Esperando autenticación
    MGMT_CMD,       // Esperando comandos
    MGMT_WRITE,     // Escribiendo respuesta
    MGMT_DONE,      // Terminado
    MGMT_ERROR,     // Error
};

// Estructura de una conexión de gestión
struct mgmt_conn {
    int fd;
    
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len;
    
    // Buffers
    uint8_t raw_read[BUFFER_SIZE];
    uint8_t raw_write[BUFFER_SIZE];
    buffer read_buffer;
    buffer write_buffer;
    
    // Estado
    struct state_machine stm;
    bool authenticated;
    
    // Buffer de línea para parsing
    char line[BUFFER_SIZE];
    size_t line_len;
    
    // Pool
    struct mgmt_conn *next;
    unsigned references;
};

// Pool de conexiones
static unsigned pool_size = 0;
static const unsigned max_pool = 10;
static struct mgmt_conn *pool = NULL;

// Forward declarations
static void mgmt_read(struct selector_key *key);
static void mgmt_write(struct selector_key *key);
static void mgmt_close(struct selector_key *key);

static const struct fd_handler mgmt_handler = {
    .handle_read   = mgmt_read,
    .handle_write  = mgmt_write,
    .handle_close  = mgmt_close,
};

// Forward declarations de estados
static void mgmt_auth_init(unsigned state, struct selector_key *key);
static unsigned mgmt_auth_read(struct selector_key *key);
static unsigned mgmt_auth_write(struct selector_key *key);
static unsigned mgmt_cmd_read(struct selector_key *key);
static unsigned mgmt_cmd_write(struct selector_key *key);
static unsigned mgmt_write_response(struct selector_key *key);

// Tabla de estados
static const struct state_definition mgmt_states[] = {
    {
        .state          = MGMT_AUTH,
        .on_arrival     = mgmt_auth_init,
        .on_read_ready  = mgmt_auth_read,
        .on_write_ready = mgmt_auth_write,
    },
    {
        .state          = MGMT_CMD,
        .on_read_ready  = mgmt_cmd_read,
        .on_write_ready = mgmt_cmd_write,
    },
    {
        .state          = MGMT_WRITE,
        .on_write_ready = mgmt_write_response,
    },
    {
        .state = MGMT_DONE,
    },
    {
        .state = MGMT_ERROR,
    },
};

#define ATTACHMENT(key) ((struct mgmt_conn *)(key)->data)

// ============================================================================
// Gestión de conexiones
// ============================================================================

static struct mgmt_conn *
mgmt_new(int fd) {
    struct mgmt_conn *m;
    
    if (pool != NULL) {
        m = pool;
        pool = pool->next;
        pool_size--;
    } else {
        m = malloc(sizeof(*m));
        if (m == NULL) return NULL;
    }
    
    memset(m, 0, sizeof(*m));
    m->fd = fd;
    m->references = 1;
    m->authenticated = false;
    
    buffer_init(&m->read_buffer, BUFFER_SIZE, m->raw_read);
    buffer_init(&m->write_buffer, BUFFER_SIZE, m->raw_write);
    
    m->stm.initial   = MGMT_AUTH;
    m->stm.max_state = MGMT_ERROR;
    m->stm.states    = mgmt_states;
    stm_init(&m->stm);
    
    return m;
}

static void
mgmt_destroy(struct mgmt_conn *m) {
    if (m == NULL) return;
    
    if (m->references == 1) {
        if (pool_size < max_pool) {
            m->next = pool;
            pool = m;
            pool_size++;
        } else {
            free(m);
        }
    } else {
        m->references--;
    }
}

void
mgmt_pool_destroy(void) {
    struct mgmt_conn *next, *m;
    for (m = pool; m != NULL; m = next) {
        next = m->next;
        free(m);
    }
    pool = NULL;
    pool_size = 0;
}

// ============================================================================
// Accept de conexiones
// ============================================================================

void
mgmt_passive_accept(struct selector_key *key) {
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    struct mgmt_conn *state = NULL;
    
    const int client = accept(key->fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client == -1) {
        goto fail;
    }
    
    if (selector_fd_set_nio(client) == -1) {
        goto fail;
    }
    
    state = mgmt_new(client);
    if (state == NULL) {
        goto fail;
    }
    
    memcpy(&state->client_addr, &client_addr, client_addr_len);
    state->client_addr_len = client_addr_len;
    
    char client_str[SOCKADDR_TO_HUMAN_MIN];
    sockaddr_to_human(client_str, sizeof(client_str), (struct sockaddr *)&client_addr);
    LOG_DEBUG("Management connection from %s", client_str);
    
    // Preparar banner en el buffer ANTES de registrar
    const char *banner = "+OK SOCKS5 Management Server v1.0\r\n"
                         "+OK Use AUTH <user> <pass> to authenticate\r\n";
    for (size_t i = 0; banner[i]; i++) {
        buffer_write(&state->write_buffer, banner[i]);
    }
    
    // Registrar con OP_WRITE para enviar el banner primero
    if (SELECTOR_SUCCESS != selector_register(key->s, client, &mgmt_handler,
                                               OP_WRITE, state)) {
        goto fail;
    }
    return;
    
fail:
    if (client != -1) {
        close(client);
    }
    mgmt_destroy(state);
}

// ============================================================================
// Utilidades de respuesta
// ============================================================================

static void
send_response(struct mgmt_conn *m, const char *response) {
    size_t len = strlen(response);
    buffer_reset(&m->write_buffer);
    
    for (size_t i = 0; i < len && buffer_can_write(&m->write_buffer); i++) {
        buffer_write(&m->write_buffer, response[i]);
    }
}

static void
send_ok(struct mgmt_conn *m, const char *msg) {
    char buf[BUFFER_SIZE];
    snprintf(buf, sizeof(buf), "+OK %s\r\n", msg);
    send_response(m, buf);
}

static void
send_err(struct mgmt_conn *m, const char *msg) {
    char buf[BUFFER_SIZE];
    snprintf(buf, sizeof(buf), "-ERR %s\r\n", msg);
    send_response(m, buf);
}

// ============================================================================
// Parsing de líneas
// ============================================================================

static bool
read_line(struct mgmt_conn *m) {
    while (buffer_can_read(&m->read_buffer)) {
        uint8_t c = buffer_read(&m->read_buffer);
        
        if (c == '\n') {
            // Eliminar \r si existe
            if (m->line_len > 0 && m->line[m->line_len - 1] == '\r') {
                m->line_len--;
            }
            m->line[m->line_len] = '\0';
            return true;
        }
        
        if (m->line_len < sizeof(m->line) - 1) {
            m->line[m->line_len++] = c;
        }
    }
    return false;
}

static void
reset_line(struct mgmt_conn *m) {
    m->line_len = 0;
    m->line[0] = '\0';
}

// ============================================================================
// Estados
// ============================================================================

static void
mgmt_auth_init(unsigned state, struct selector_key *key) {
    (void)state;
    (void)key;
    // El banner ya fue enviado en mgmt_passive_accept
}

/**
 * Escribe el banner o respuesta pendiente en estado AUTH
 */
static unsigned
mgmt_auth_write(struct selector_key *key) {
    struct mgmt_conn *m = ATTACHMENT(key);
    
    uint8_t *ptr;
    size_t count;
    ssize_t n;
    
    ptr = buffer_read_ptr(&m->write_buffer, &count);
    if (count == 0) {
        selector_set_interest_key(key, OP_READ);
        return MGMT_AUTH;
    }
    
    n = send(m->fd, ptr, count, MSG_NOSIGNAL);
    if (n <= 0) {
        return MGMT_ERROR;
    }
    
    buffer_read_adv(&m->write_buffer, n);
    
    if (buffer_can_read(&m->write_buffer)) {
        return MGMT_AUTH;  // Más datos por escribir
    }
    
    // Terminamos de escribir, volver a esperar lectura
    selector_set_interest_key(key, OP_READ);
    return MGMT_AUTH;
}

static unsigned
mgmt_auth_read(struct selector_key *key) {
    struct mgmt_conn *m = ATTACHMENT(key);
    
    uint8_t *ptr;
    size_t count;
    ssize_t n;
    
    ptr = buffer_write_ptr(&m->read_buffer, &count);
    n = recv(key->fd, ptr, count, 0);
    
    if (n <= 0) {
        return MGMT_ERROR;
    }
    
    buffer_write_adv(&m->read_buffer, n);
    
    if (!read_line(m)) {
        return MGMT_AUTH;
    }
    
    // Parsear comando AUTH
    char user[256], pass[256];
    if (sscanf(m->line, "AUTH %255s %255s", user, pass) == 2) {
        if (strcmp(user, ADMIN_USER) == 0 && strcmp(pass, ADMIN_PASS) == 0) {
            m->authenticated = true;
            send_ok(m, "Authenticated successfully. Type HELP for commands.");
            reset_line(m);
            selector_set_interest_key(key, OP_WRITE);
            return MGMT_CMD;
        }
    }
    
    send_err(m, "Authentication failed");
    reset_line(m);
    selector_set_interest_key(key, OP_WRITE);
    return MGMT_AUTH;
}

/**
 * Escribe respuesta en estado CMD
 */
static unsigned
mgmt_cmd_write(struct selector_key *key) {
    struct mgmt_conn *m = ATTACHMENT(key);
    
    uint8_t *ptr;
    size_t count;
    ssize_t n;
    
    ptr = buffer_read_ptr(&m->write_buffer, &count);
    if (count == 0) {
        selector_set_interest_key(key, OP_READ);
        return MGMT_CMD;
    }
    
    n = send(m->fd, ptr, count, MSG_NOSIGNAL);
    if (n <= 0) {
        return MGMT_ERROR;
    }
    
    buffer_read_adv(&m->write_buffer, n);
    
    if (buffer_can_read(&m->write_buffer)) {
        return MGMT_CMD;  // Más datos por escribir
    }
    
    // Terminamos de escribir, volver a esperar lectura
    selector_set_interest_key(key, OP_READ);
    return MGMT_CMD;
}

// Callback para listar usuarios
static void
list_users_callback(const char *username, void *ctx) {
    struct mgmt_conn *m = ctx;
    char line[300];
    snprintf(line, sizeof(line), "+OK USER %s\r\n", username);
    
    for (size_t i = 0; line[i] && buffer_can_write(&m->write_buffer); i++) {
        buffer_write(&m->write_buffer, line[i]);
    }
}

static unsigned
mgmt_cmd_read(struct selector_key *key) {
    struct mgmt_conn *m = ATTACHMENT(key);
    
    uint8_t *ptr;
    size_t count;
    ssize_t n;
    
    ptr = buffer_write_ptr(&m->read_buffer, &count);
    n = recv(key->fd, ptr, count, 0);
    
    if (n <= 0) {
        return MGMT_DONE;
    }
    
    buffer_write_adv(&m->read_buffer, n);
    
    if (!read_line(m)) {
        return MGMT_CMD;
    }
    
    // Parsear comando
    char cmd[32];
    if (sscanf(m->line, "%31s", cmd) != 1) {
        send_err(m, "Invalid command");
        reset_line(m);
        selector_set_interest_key(key, OP_WRITE);
        return MGMT_CMD;
    }
    
    if (strcasecmp(cmd, "QUIT") == 0) {
        send_ok(m, "Bye");
        reset_line(m);
        selector_set_interest_key(key, OP_WRITE);
        return MGMT_DONE;
    }
    
    if (strcasecmp(cmd, "HELP") == 0) {
        const char *help = 
            "+OK Commands:\r\n"
            "+OK   STATS                 - Show server statistics\r\n"
            "+OK   USERS                 - List proxy users\r\n"
            "+OK   ADDUSER <user> <pass> - Add a proxy user\r\n"
            "+OK   DELUSER <user>        - Delete a proxy user\r\n"
            "+OK   HELP                  - Show this help\r\n"
            "+OK   QUIT                  - Close connection\r\n"
            "+OK End of help\r\n";
        send_response(m, help);
        reset_line(m);
        selector_set_interest_key(key, OP_WRITE);
        return MGMT_CMD;
    }
    
    if (strcasecmp(cmd, "STATS") == 0) {
        struct server_metrics *met = metrics_get();
        char stats[1024];
        snprintf(stats, sizeof(stats),
            "+OK Statistics:\r\n"
            "+OK   Total connections:    %lu\r\n"
            "+OK   Current connections:  %lu\r\n"
            "+OK   Bytes transferred:    %lu\r\n"
            "+OK   Bytes sent:           %lu\r\n"
            "+OK   Bytes received:       %lu\r\n"
            "+OK   Successful conns:     %lu\r\n"
            "+OK   Failed conns:         %lu\r\n"
            "+OK End of statistics\r\n",
            (unsigned long)atomic_load(&met->total_connections),
            (unsigned long)atomic_load(&met->current_connections),
            (unsigned long)atomic_load(&met->bytes_transferred),
            (unsigned long)atomic_load(&met->bytes_sent),
            (unsigned long)atomic_load(&met->bytes_received),
            (unsigned long)atomic_load(&met->successful_connections),
            (unsigned long)atomic_load(&met->failed_connections));
        send_response(m, stats);
        reset_line(m);
        selector_set_interest_key(key, OP_WRITE);
        return MGMT_CMD;
    }
    
    if (strcasecmp(cmd, "USERS") == 0) {
        buffer_reset(&m->write_buffer);
        const char *header = "+OK User list:\r\n";
        for (size_t i = 0; header[i]; i++) {
            buffer_write(&m->write_buffer, header[i]);
        }
        users_foreach(list_users_callback, m);
        const char *footer = "+OK End of user list\r\n";
        for (size_t i = 0; footer[i] && buffer_can_write(&m->write_buffer); i++) {
            buffer_write(&m->write_buffer, footer[i]);
        }
        reset_line(m);
        selector_set_interest_key(key, OP_WRITE);
        return MGMT_CMD;
    }
    
    if (strcasecmp(cmd, "ADDUSER") == 0) {
        char user[256], pass[256];
        if (sscanf(m->line, "%*s %255s %255s", user, pass) == 2) {
            if (users_add(user, pass)) {
                LOG_INFO("Admin added user: %s", user);
                send_ok(m, "User added successfully");
            } else {
                send_err(m, "Failed to add user");
            }
        } else {
            send_err(m, "Usage: ADDUSER <username> <password>");
        }
        reset_line(m);
        selector_set_interest_key(key, OP_WRITE);
        return MGMT_CMD;
    }
    
    if (strcasecmp(cmd, "DELUSER") == 0) {
        char user[256];
        if (sscanf(m->line, "%*s %255s", user) == 1) {
            if (users_remove(user)) {
                LOG_INFO("Admin removed user: %s", user);
                send_ok(m, "User removed successfully");
            } else {
                send_err(m, "User not found");
            }
        } else {
            send_err(m, "Usage: DELUSER <username>");
        }
        reset_line(m);
        selector_set_interest_key(key, OP_WRITE);
        return MGMT_CMD;
    }
    
    send_err(m, "Unknown command. Type HELP for available commands.");
    reset_line(m);
    selector_set_interest_key(key, OP_WRITE);
    return MGMT_CMD;
}

static unsigned
mgmt_write_response(struct selector_key *key) {
    struct mgmt_conn *m = ATTACHMENT(key);
    
    uint8_t *ptr;
    size_t count;
    ssize_t n;
    
    ptr = buffer_read_ptr(&m->write_buffer, &count);
    n = send(key->fd, ptr, count, MSG_NOSIGNAL);
    
    if (n <= 0) {
        return MGMT_ERROR;
    }
    
    buffer_read_adv(&m->write_buffer, n);
    
    if (buffer_can_read(&m->write_buffer)) {
        return MGMT_WRITE;
    }
    
    // Volver a leer comandos
    selector_set_interest_key(key, OP_READ);
    
    // Determinar siguiente estado según si estamos autenticados
    return m->authenticated ? MGMT_CMD : MGMT_AUTH;
}

// ============================================================================
// Handlers del selector
// ============================================================================

static void
mgmt_done(struct selector_key *key);

static void
mgmt_read(struct selector_key *key) {
    struct state_machine *stm = &ATTACHMENT(key)->stm;
    const enum mgmt_state st = stm_handler_read(stm, key);
    
    if (MGMT_ERROR == st || MGMT_DONE == st) {
        mgmt_done(key);
    }
}

static void
mgmt_write(struct selector_key *key) {
    struct state_machine *stm = &ATTACHMENT(key)->stm;
    const enum mgmt_state st = stm_handler_write(stm, key);
    
    if (MGMT_ERROR == st || MGMT_DONE == st) {
        mgmt_done(key);
    }
}

static void
mgmt_close(struct selector_key *key) {
    mgmt_destroy(ATTACHMENT(key));
}

static void
mgmt_done(struct selector_key *key) {
    struct mgmt_conn *m = ATTACHMENT(key);
    
    if (m->fd >= 0) {
        selector_unregister_fd(key->s, m->fd);
        close(m->fd);
        m->fd = -1;
    }
}

