/**
 * logger.c - Sistema de logging para el servidor
 *
 * Implementa logging con niveles y registro de acceso para auditoría.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "logger.h"
#include "netutils.h"

// Configuración del logger
static log_level_t current_level = LOG_INFO;
static FILE *log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// Nombres de niveles
static const char *level_names[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
};

// Colores ANSI para terminal
static const char *level_colors[] = {
    "\x1b[36m",   // DEBUG: cyan
    "\x1b[32m",   // INFO: green
    "\x1b[33m",   // WARN: yellow
    "\x1b[31m",   // ERROR: red
    "\x1b[35m"    // FATAL: magenta
};
static const char *color_reset = "\x1b[0m";

void
logger_init(log_level_t level, const char *filename) {
    current_level = level;
    
    if (filename != NULL) {
        log_file = fopen(filename, "a");
        if (log_file == NULL) {
            fprintf(stderr, "Warning: Could not open log file %s, using stderr\n", filename);
            log_file = stderr;
        }
    } else {
        log_file = stderr;
    }
}

void
logger_close(void) {
    pthread_mutex_lock(&log_mutex);
    if (log_file != NULL && log_file != stderr) {
        fclose(log_file);
    }
    log_file = NULL;
    pthread_mutex_unlock(&log_mutex);
}

void
logger_set_level(log_level_t level) {
    current_level = level;
}

void
log_msg(log_level_t level, const char *fmt, ...) {
    if (level < current_level) {
        return;
    }
    
    pthread_mutex_lock(&log_mutex);
    
    if (log_file == NULL) {
        log_file = stderr;
    }
    
    // Obtener timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Determinar si usar colores (solo para stderr que es terminal)
    bool use_colors = (log_file == stderr);
    
    // Escribir log
    if (use_colors) {
        fprintf(log_file, "%s[%s] %s%-5s%s: ", 
                color_reset, time_buf, 
                level_colors[level], level_names[level], color_reset);
    } else {
        fprintf(log_file, "[%s] %-5s: ", time_buf, level_names[level]);
    }
    
    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);
    
    fprintf(log_file, "\n");
    fflush(log_file);
    
    pthread_mutex_unlock(&log_mutex);
}

void
log_access(const char *user, 
           const struct sockaddr *client_addr,
           const char *target_host,
           uint16_t target_port,
           const char *status,
           uint64_t bytes_sent,
           uint64_t bytes_recv) {
    
    pthread_mutex_lock(&log_mutex);
    
    if (log_file == NULL) {
        log_file = stderr;
    }
    
    // Obtener timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Convertir dirección del cliente a string
    char client_str[SOCKADDR_TO_HUMAN_MIN];
    sockaddr_to_human(client_str, sizeof(client_str), client_addr);
    
    // Formato de log de acceso:
    // [timestamp] ACCESS user@client -> host:port status sent/recv
    fprintf(log_file, "[%s] ACCESS %s@%s -> %s:%u %s %lu/%lu\n",
            time_buf,
            user ? user : "-",
            client_str,
            target_host ? target_host : "-",
            target_port,
            status ? status : "-",
            (unsigned long)bytes_sent,
            (unsigned long)bytes_recv);
    
    fflush(log_file);
    
    pthread_mutex_unlock(&log_mutex);
}

