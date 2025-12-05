/**
 * logger.h - Sistema de logging para el servidor
 *
 * Requerimiento funcional 8: implementar un registro de acceso que permita
 * a un administrador entender los accesos de cada uno de los usuarios.
 */
#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>
#include <stdbool.h>
#include <time.h>
#include <netinet/in.h>

/**
 * Niveles de log
 */
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_FATAL
} log_level_t;

/**
 * Inicializa el sistema de logging.
 * 
 * @param level Nivel mínimo de log a registrar
 * @param log_file Archivo donde escribir logs (NULL para stderr)
 */
void logger_init(log_level_t level, const char *log_file);

/**
 * Cierra el sistema de logging.
 */
void logger_close(void);

/**
 * Establece el nivel de log.
 */
void logger_set_level(log_level_t level);

/**
 * Registra un mensaje de log.
 */
void log_msg(log_level_t level, const char *fmt, ...);

/**
 * Registra un acceso SOCKS5 (para auditoría).
 * 
 * @param user Usuario que realizó la conexión
 * @param client_addr Dirección del cliente
 * @param target_host Host destino
 * @param target_port Puerto destino
 * @param status Estado de la conexión ("OK", "AUTH_FAILED", "CONN_REFUSED", etc)
 * @param bytes_sent Bytes enviados al cliente
 * @param bytes_recv Bytes recibidos del cliente
 */
void log_access(const char *user, 
                const struct sockaddr *client_addr,
                const char *target_host,
                uint16_t target_port,
                const char *status,
                uint64_t bytes_sent,
                uint64_t bytes_recv);

// Macros de conveniencia (C11 compatible)
#define LOG_DEBUG(...) log_msg(LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  log_msg(LOG_INFO, __VA_ARGS__)
#define LOG_WARN(...)  log_msg(LOG_WARNING, __VA_ARGS__)
#define LOG_ERROR(...) log_msg(LOG_ERROR, __VA_ARGS__)
#define LOG_FATAL(...) log_msg(LOG_FATAL, __VA_ARGS__)

#endif

