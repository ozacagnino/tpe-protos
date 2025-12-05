/**
 * args.c - Parsing de argumentos de línea de comandos
 * 
 * Implementa el parsing según IEEE Std 1003.1-2008, 2016 Edition
 * (POSIX Utility Conventions)
 */
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include "args.h"

static unsigned short
port(const char *s) {
    char *end = 0;
    const long sl = strtol(s, &end, 10);

    if (end == s || '\0' != *end
        || ((LONG_MIN == sl || LONG_MAX == sl) && ERANGE == errno)
        || sl < 0 || sl > USHRT_MAX) {
        fprintf(stderr, "port should be in the range of 1-65535: %s\n", s);
        exit(1);
    }
    return (unsigned short)sl;
}

static void
user(char *s, struct users *user) {
    char *p = strchr(s, ':');
    if (p == NULL) {
        fprintf(stderr, "password not found for user: %s\n", s);
        exit(1);
    } else {
        *p = 0;
        p++;
        user->name = s;
        user->pass = p;
    }
}

static void
version(void) {
    fprintf(stderr, "socks5d version 1.0\n"
            "ITBA Protocolos de Comunicación 2025/2 -- Grupo X\n"
            "Licencia MIT\n");
}

static void
usage(const char *progname) {
    fprintf(stderr,
            "Usage: %s [OPTION]...\n"
            "\n"
            "   -h               Imprime la ayuda y termina.\n"
            "   -l <SOCKS addr>  Dirección donde servirá el proxy SOCKS (default: 0.0.0.0).\n"
            "   -L <conf  addr>  Dirección donde servirá el servicio de management (default: 127.0.0.1).\n"
            "   -p <SOCKS port>  Puerto entrante conexiones SOCKS (default: 1080).\n"
            "   -P <conf port>   Puerto entrante conexiones configuracion (default: 8080).\n"
            "   -u <name>:<pass> Usuario y contraseña de usuario que puede usar el proxy. Hasta %d.\n"
            "   -N               Desactiva los disectores de credenciales.\n"
            "   -v               Imprime información sobre la versión y termina.\n"
            "\n",
            progname, MAX_USERS);
    exit(1);
}

void
parse_args(const int argc, char **argv, struct socks5args *args) {
    memset(args, 0, sizeof(*args));

    // Valores por defecto - RFC 1928 sugiere puerto 1080 para SOCKS
    args->socks_addr = "0.0.0.0";
    args->socks_port = 1080;

    // Management solo en loopback por seguridad
    args->mng_addr = "127.0.0.1";
    args->mng_port = 8080;

    args->disectors_enabled = true;
    args->nusers = 0;

    int c;

    while (true) {
        int option_index = 0;
        static struct option long_options[] = {
            { "help",    no_argument,       0, 'h' },
            { "version", no_argument,       0, 'v' },
            { 0,         0,                 0,  0  }
        };

        c = getopt_long(argc, argv, "hl:L:Np:P:u:v", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'h':
            usage(argv[0]);
            break;
        case 'l':
            args->socks_addr = optarg;
            break;
        case 'L':
            args->mng_addr = optarg;
            break;
        case 'N':
            args->disectors_enabled = false;
            break;
        case 'p':
            args->socks_port = port(optarg);
            break;
        case 'P':
            args->mng_port = port(optarg);
            break;
        case 'u':
            if (args->nusers >= MAX_USERS) {
                fprintf(stderr, "Maximum number of command line users reached: %d.\n", MAX_USERS);
                exit(1);
            } else {
                user(optarg, args->users + args->nusers);
                args->nusers++;
            }
            break;
        case 'v':
            version();
            exit(0);
        default:
            fprintf(stderr, "Unknown argument %d.\n", c);
            exit(1);
        }
    }
    if (optind < argc) {
        fprintf(stderr, "Argument not accepted: ");
        while (optind < argc) {
            fprintf(stderr, "%s ", argv[optind++]);
        }
        fprintf(stderr, "\n");
        exit(1);
    }
}

