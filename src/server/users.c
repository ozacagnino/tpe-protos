/**
 * users.c - Gestión de usuarios del proxy SOCKS5
 *
 * Implementa RFC 1929: Username/Password Authentication for SOCKS V5
 *
 * Los usuarios se almacenan en memoria (volátiles).
 * Se usa un mutex para thread-safety (aunque en esta implementación
 * single-threaded no es estrictamente necesario).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "users.h"

// Estructura interna para un usuario
struct user_entry {
    char username[MAX_USERNAME_LEN + 1];
    char password[MAX_PASSWORD_LEN + 1];
    bool active;
};

// Base de datos de usuarios
static struct user_entry users_db[MAX_TOTAL_USERS];
static int users_count_val = 0;
static pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;

void
users_init(void) {
    pthread_mutex_lock(&users_mutex);
    memset(users_db, 0, sizeof(users_db));
    users_count_val = 0;
    pthread_mutex_unlock(&users_mutex);
}

void
users_destroy(void) {
    pthread_mutex_lock(&users_mutex);
    // Limpiar memoria sensible (contraseñas)
    for (int i = 0; i < MAX_TOTAL_USERS; i++) {
        if (users_db[i].active) {
            memset(users_db[i].password, 0, sizeof(users_db[i].password));
        }
    }
    memset(users_db, 0, sizeof(users_db));
    users_count_val = 0;
    pthread_mutex_unlock(&users_mutex);
}

bool
users_add(const char *username, const char *password) {
    if (username == NULL || password == NULL) {
        return false;
    }
    
    size_t ulen = strlen(username);
    size_t plen = strlen(password);
    
    // RFC 1929: username y password máximo 255 bytes
    if (ulen == 0 || ulen > MAX_USERNAME_LEN || 
        plen == 0 || plen > MAX_PASSWORD_LEN) {
        return false;
    }
    
    bool result = false;
    pthread_mutex_lock(&users_mutex);
    
    // Verificar si el usuario ya existe
    for (int i = 0; i < MAX_TOTAL_USERS; i++) {
        if (users_db[i].active && 
            strcmp(users_db[i].username, username) == 0) {
            // Usuario ya existe, actualizar contraseña
            strncpy(users_db[i].password, password, MAX_PASSWORD_LEN);
            users_db[i].password[MAX_PASSWORD_LEN] = '\0';
            result = true;
            goto unlock;
        }
    }
    
    // Buscar slot libre
    for (int i = 0; i < MAX_TOTAL_USERS; i++) {
        if (!users_db[i].active) {
            strncpy(users_db[i].username, username, MAX_USERNAME_LEN);
            users_db[i].username[MAX_USERNAME_LEN] = '\0';
            strncpy(users_db[i].password, password, MAX_PASSWORD_LEN);
            users_db[i].password[MAX_PASSWORD_LEN] = '\0';
            users_db[i].active = true;
            users_count_val++;
            result = true;
            goto unlock;
        }
    }
    
unlock:
    pthread_mutex_unlock(&users_mutex);
    return result;
}

bool
users_remove(const char *username) {
    if (username == NULL) {
        return false;
    }
    
    bool result = false;
    pthread_mutex_lock(&users_mutex);
    
    for (int i = 0; i < MAX_TOTAL_USERS; i++) {
        if (users_db[i].active && 
            strcmp(users_db[i].username, username) == 0) {
            // Limpiar datos sensibles
            memset(users_db[i].password, 0, sizeof(users_db[i].password));
            memset(users_db[i].username, 0, sizeof(users_db[i].username));
            users_db[i].active = false;
            users_count_val--;
            result = true;
            break;
        }
    }
    
    pthread_mutex_unlock(&users_mutex);
    return result;
}

bool
users_verify(const char *username, const char *password) {
    if (username == NULL || password == NULL) {
        return false;
    }
    
    bool result = false;
    pthread_mutex_lock(&users_mutex);
    
    for (int i = 0; i < MAX_TOTAL_USERS; i++) {
        if (users_db[i].active && 
            strcmp(users_db[i].username, username) == 0) {
            // Comparación de contraseña (timing-safe sería mejor)
            if (strcmp(users_db[i].password, password) == 0) {
                result = true;
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&users_mutex);
    return result;
}

bool
users_exists(const char *username) {
    if (username == NULL) {
        return false;
    }
    
    bool result = false;
    pthread_mutex_lock(&users_mutex);
    
    for (int i = 0; i < MAX_TOTAL_USERS; i++) {
        if (users_db[i].active && 
            strcmp(users_db[i].username, username) == 0) {
            result = true;
            break;
        }
    }
    
    pthread_mutex_unlock(&users_mutex);
    return result;
}

int
users_count(void) {
    pthread_mutex_lock(&users_mutex);
    int count = users_count_val;
    pthread_mutex_unlock(&users_mutex);
    return count;
}

void
users_foreach(void (*callback)(const char *username, void *ctx), void *ctx) {
    if (callback == NULL) {
        return;
    }
    
    pthread_mutex_lock(&users_mutex);
    
    for (int i = 0; i < MAX_TOTAL_USERS; i++) {
        if (users_db[i].active) {
            callback(users_db[i].username, ctx);
        }
    }
    
    pthread_mutex_unlock(&users_mutex);
}

