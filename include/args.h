/**
 * args.h - Parsing de argumentos de línea de comandos
 * 
 * Soporta:
 *   -h               Imprime la ayuda y termina.
 *   -l <SOCKS addr>  Dirección donde servirá el proxy SOCKS.
 *   -L <conf  addr>  Dirección donde servirá el servicio de management.
 *   -p <SOCKS port>  Puerto entrante conexiones SOCKS.
 *   -P <conf port>   Puerto entrante conexiones configuracion
 *   -u <name>:<pass> Usuario y contraseña de usuario que puede usar el proxy.
 *   -v               Imprime información sobre la versión y termina.
 */
#ifndef ARGS_H_kFlmYm1tW9p5npzDr2opQJ9jM8
#define ARGS_H_kFlmYm1tW9p5npzDr2opQJ9jM8

#include <stdbool.h>

#define MAX_USERS 10

struct users {
    char *name;
    char *pass;
};

struct socks5args {
    char           *socks_addr;
    unsigned short  socks_port;

    char           *mng_addr;
    unsigned short  mng_port;

    bool            disectors_enabled;

    struct users    users[MAX_USERS];
    int             nusers;
};

/**
 * Interpreta la linea de comandos (argc, argv) llenando
 * args con defaults o la seleccion humana. Puede cortar
 * la ejecución.
 */
void
parse_args(const int argc, char **argv, struct socks5args *args);

#endif

