/**
 * client.c - Cliente de gestión para el servidor SOCKS5
 *
 * Permite:
 *   - Conectarse al puerto de gestión
 *   - Autenticarse como administrador
 *   - Enviar comandos para gestionar usuarios y ver métricas
 *
 * Uso:
 *   ./client [-L <addr>] [-P <port>] [-u <user>] [-p <pass>]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  // strcasecmp
#include <errno.h>
#include <stdbool.h>
#include <getopt.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 4096

// Configuración por defecto
static char *mgmt_addr = "127.0.0.1";
static unsigned short mgmt_port = 8080;
static char *admin_user = "admin";
static char *admin_pass = "admin123";

static void
usage(const char *progname) {
    fprintf(stderr,
            "Usage: %s [OPTIONS]\n"
            "\n"
            "Options:\n"
            "   -h               Show this help\n"
            "   -L <addr>        Management server address (default: 127.0.0.1)\n"
            "   -P <port>        Management server port (default: 8080)\n"
            "   -u <user>        Admin username (default: admin)\n"
            "   -p <pass>        Admin password (default: admin123)\n"
            "\n"
            "Interactive commands:\n"
            "   STATS            Show server statistics\n"
            "   USERS            List proxy users\n"
            "   ADDUSER u p      Add proxy user\n"
            "   DELUSER u        Delete proxy user\n"
            "   HELP             Show available commands\n"
            "   QUIT             Close connection\n"
            "\n",
            progname);
    exit(1);
}

static void
parse_args(int argc, char **argv) {
    int c;
    
    while ((c = getopt(argc, argv, "hL:P:u:p:")) != -1) {
        switch (c) {
            case 'h':
                usage(argv[0]);
                break;
            case 'L':
                mgmt_addr = optarg;
                break;
            case 'P':
                mgmt_port = (unsigned short)atoi(optarg);
                break;
            case 'u':
                admin_user = optarg;
                break;
            case 'p':
                admin_pass = optarg;
                break;
            default:
                usage(argv[0]);
        }
    }
}

/**
 * Conecta al servidor de gestión
 */
static int
connect_to_server(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(mgmt_port);
    
    if (inet_pton(AF_INET, mgmt_addr, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid address: %s\n", mgmt_addr);
        close(sock);
        return -1;
    }
    
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }
    
    return sock;
}

/**
 * Lee respuesta del servidor hasta timeout o línea vacía
 */
static void
read_response(int sock) {
    char buf[BUFFER_SIZE];
    ssize_t n;
    
    // Usar timeout corto para lectura
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
        fflush(stdout);
    }
}

/**
 * Envía un comando al servidor
 */
static void
send_command(int sock, const char *cmd) {
    char buf[BUFFER_SIZE];
    snprintf(buf, sizeof(buf), "%s\r\n", cmd);
    
    if (send(sock, buf, strlen(buf), 0) < 0) {
        perror("send");
    }
}

int
main(int argc, char **argv) {
    parse_args(argc, argv);
    
    printf("SOCKS5 Management Client\n");
    printf("Connecting to %s:%d...\n", mgmt_addr, mgmt_port);
    
    int sock = connect_to_server();
    if (sock < 0) {
        return 1;
    }
    
    printf("Connected!\n\n");
    
    // Leer banner
    read_response(sock);
    
    // Autenticar
    char auth_cmd[512];
    snprintf(auth_cmd, sizeof(auth_cmd), "AUTH %s %s", admin_user, admin_pass);
    printf("> %s\n", auth_cmd);
    send_command(sock, auth_cmd);
    read_response(sock);
    
    printf("\nEnter commands (HELP for list, QUIT to exit):\n");
    
    // Loop interactivo
    char line[BUFFER_SIZE];
    while (true) {
        printf("> ");
        fflush(stdout);
        
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }
        
        // Eliminar newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        // Ignorar líneas vacías
        if (strlen(line) == 0) {
            continue;
        }
        
        // Verificar si es QUIT local
        if (strcasecmp(line, "QUIT") == 0 || strcasecmp(line, "EXIT") == 0) {
            send_command(sock, "QUIT");
            read_response(sock);
            break;
        }
        
        // Enviar comando
        send_command(sock, line);
        read_response(sock);
    }
    
    close(sock);
    printf("Connection closed.\n");
    
    return 0;
}

