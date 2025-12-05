/**
 * socks5nio.h - Servidor proxy SOCKSv5 no bloqueante
 *
 * Implementa RFC 1928 (SOCKS Protocol Version 5) y
 * RFC 1929 (Username/Password Authentication for SOCKS V5)
 */
#ifndef SOCKS5NIO_H
#define SOCKS5NIO_H

#include <netdb.h>
#include "selector.h"

/**
 * Obtiene el handler para el socket pasivo de SOCKS5.
 * Usado por el selector para aceptar nuevas conexiones.
 */
void
socksv5_passive_accept(struct selector_key *key);

/**
 * Libera el pool de estructuras socks5 reutilizables.
 * Debe llamarse al terminar el servidor.
 */
void
socksv5_pool_destroy(void);

#endif

