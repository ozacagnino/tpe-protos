/**
 * users.h - Gestión de usuarios del proxy SOCKS5
 *
 * Requerimiento funcional 7: implementar mecanismos que permitan manejar usuarios
 * o cambiar la configuración del servidor en tiempo de ejecución.
 */
#ifndef USERS_H
#define USERS_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_USERNAME_LEN 255
#define MAX_PASSWORD_LEN 255
#define MAX_TOTAL_USERS  100

/**
 * Inicializa el sistema de usuarios.
 */
void users_init(void);

/**
 * Libera recursos del sistema de usuarios.
 */
void users_destroy(void);

/**
 * Agrega un usuario al sistema.
 * 
 * @param username Nombre de usuario
 * @param password Contraseña
 * @return true si se agregó correctamente, false si ya existe o hay error
 */
bool users_add(const char *username, const char *password);

/**
 * Elimina un usuario del sistema.
 * 
 * @param username Nombre de usuario a eliminar
 * @return true si se eliminó correctamente, false si no existe
 */
bool users_remove(const char *username);

/**
 * Verifica las credenciales de un usuario (RFC 1929).
 * 
 * @param username Nombre de usuario
 * @param password Contraseña
 * @return true si las credenciales son válidas
 */
bool users_verify(const char *username, const char *password);

/**
 * Verifica si un usuario existe.
 * 
 * @param username Nombre de usuario
 * @return true si el usuario existe
 */
bool users_exists(const char *username);

/**
 * Obtiene la cantidad de usuarios registrados.
 */
int users_count(void);

/**
 * Itera sobre todos los usuarios.
 * 
 * @param callback Función a llamar por cada usuario
 * @param ctx Contexto a pasar al callback
 */
void users_foreach(void (*callback)(const char *username, void *ctx), void *ctx);

#endif

