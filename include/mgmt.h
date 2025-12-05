/**
 * mgmt.h - Servidor de gestión/monitoreo
 *
 * Protocolo de gestión basado en texto para:
 *   - Consultar métricas (conexiones históricas, actuales, bytes transferidos)
 *   - Gestionar usuarios (agregar/eliminar en runtime)
 *   - Ver logs de acceso
 */
#ifndef MGMT_H
#define MGMT_H

#include "selector.h"

/**
 * Handler para aceptar conexiones en el socket de gestión.
 */
void
mgmt_passive_accept(struct selector_key *key);

/**
 * Libera recursos del módulo de gestión.
 */
void
mgmt_pool_destroy(void);

#endif

