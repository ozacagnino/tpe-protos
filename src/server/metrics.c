/**
 * metrics.c - Sistema de métricas para monitoreo del servidor
 *
 * Implementación thread-safe usando variables atómicas de C11.
 */
#include <string.h>
#include "metrics.h"

// Métricas globales del servidor
static struct server_metrics metrics;

void
metrics_init(void) {
    memset(&metrics, 0, sizeof(metrics));
    atomic_init(&metrics.total_connections, 0);
    atomic_init(&metrics.current_connections, 0);
    atomic_init(&metrics.bytes_transferred, 0);
    atomic_init(&metrics.successful_connections, 0);
    atomic_init(&metrics.failed_connections, 0);
    atomic_init(&metrics.bytes_sent, 0);
    atomic_init(&metrics.bytes_received, 0);
}

struct server_metrics *
metrics_get(void) {
    return &metrics;
}

void
metrics_connection_opened(void) {
    atomic_fetch_add(&metrics.total_connections, 1);
    atomic_fetch_add(&metrics.current_connections, 1);
}

void
metrics_connection_closed(void) {
    atomic_fetch_sub(&metrics.current_connections, 1);
}

void
metrics_connection_success(void) {
    atomic_fetch_add(&metrics.successful_connections, 1);
}

void
metrics_connection_failed(void) {
    atomic_fetch_add(&metrics.failed_connections, 1);
}

void
metrics_add_bytes_transferred(uint64_t bytes) {
    atomic_fetch_add(&metrics.bytes_transferred, bytes);
}

void
metrics_add_bytes_sent(uint64_t bytes) {
    atomic_fetch_add(&metrics.bytes_sent, bytes);
    atomic_fetch_add(&metrics.bytes_transferred, bytes);
}

void
metrics_add_bytes_received(uint64_t bytes) {
    atomic_fetch_add(&metrics.bytes_received, bytes);
    atomic_fetch_add(&metrics.bytes_transferred, bytes);
}

