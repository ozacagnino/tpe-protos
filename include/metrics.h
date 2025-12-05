/**
 * metrics.h - Sistema de métricas para monitoreo del servidor
 *
 * Requerimiento funcional 6: implementar mecanismos que permitan recolectar
 * métricas que ayuden a monitorear la operación del sistema.
 */
#ifndef METRICS_H
#define METRICS_H

#include <stdint.h>
#include <stdatomic.h>

/**
 * Estructura con las métricas del servidor.
 * Todas las métricas son volátiles (se pierden al reiniciar).
 */
struct server_metrics {
    /** Cantidad de conexiones históricas (total desde inicio) */
    _Atomic uint64_t total_connections;
    
    /** Cantidad de conexiones concurrentes actuales */
    _Atomic uint64_t current_connections;
    
    /** Cantidad total de bytes transferidos */
    _Atomic uint64_t bytes_transferred;
    
    /** Conexiones SOCKS exitosas */
    _Atomic uint64_t successful_connections;
    
    /** Conexiones fallidas (auth error, connect error, etc) */
    _Atomic uint64_t failed_connections;
    
    /** Bytes enviados al cliente (downstream) */
    _Atomic uint64_t bytes_sent;
    
    /** Bytes recibidos del cliente (upstream) */
    _Atomic uint64_t bytes_received;
};

/**
 * Inicializa el sistema de métricas.
 */
void metrics_init(void);

/**
 * Obtiene un puntero a la estructura de métricas global.
 */
struct server_metrics *metrics_get(void);

/**
 * Incrementa el contador de conexiones totales y actuales.
 */
void metrics_connection_opened(void);

/**
 * Decrementa el contador de conexiones actuales.
 */
void metrics_connection_closed(void);

/**
 * Marca una conexión como exitosa.
 */
void metrics_connection_success(void);

/**
 * Marca una conexión como fallida.
 */
void metrics_connection_failed(void);

/**
 * Agrega bytes transferidos a las métricas.
 */
void metrics_add_bytes_transferred(uint64_t bytes);

/**
 * Agrega bytes enviados al cliente.
 */
void metrics_add_bytes_sent(uint64_t bytes);

/**
 * Agrega bytes recibidos del cliente.
 */
void metrics_add_bytes_received(uint64_t bytes);

#endif

