# Trabajo Práctico Especial - Protocolos de Comunicación

## Servidor Proxy SOCKSv5

**Materia:** 72.07 - Protocolos de Comunicación  
**Cuatrimestre:** 2C 2025  
**Institución:** Instituto Tecnológico de Buenos Aires (ITBA)

## Descripción

Implementación completa de un servidor proxy SOCKSv5 según RFC 1928, con:

- **Autenticación usuario/contraseña** (RFC 1929)
- **Soporte para IPv4, IPv6 y FQDN**
- **I/O no bloqueante** con multiplexación mediante `select()`
- **Resolución DNS asíncrona** mediante threads auxiliares
- **Protocolo de gestión y monitoreo** con comandos en tiempo de ejecución
- **Métricas del servidor** (conexiones, bytes transferidos, etc.)
- **Registro de acceso** para auditoría

---

## Integrantes

| Nombre | Legajo | Email |
|--------|--------|-------|
| Octavio Zacagnino | 64255 | ozacagnino@itba.edu.ar |
| Facundo Lasserre | 62165 | flasserre@itba.edu.ar |

---

## Ubicación de Materiales

Los materiales de entrega se encuentran organizados de la siguiente manera:

| Material | Ubicación | Descripción |
|----------|-----------|-------------|
| **Informe Técnico** | `docs/informe.md` | Informe completo en formato Markdown |
| **Consigna** | `docs/consigna.txt` | Enunciado del trabajo práctico |
| **Código Fuente Servidor** | `src/server/` | Implementación del servidor SOCKS5 |
| **Código Fuente Cliente** | `src/client/` | Cliente de gestión/monitoreo |
| **Bibliotecas** | `src/lib/` | Framework de la cátedra (selector, buffer, stm) |
| **Headers** | `include/` | Archivos de cabecera (.h) |
| **Sistema de Build** | `Makefile` | Archivo de construcción |
| **Pruebas de Estrés** | `docs/stress_test.py` | Script de pruebas de rendimiento |
| **Resultados de Estrés** | `docs/stress_report.md` | Informe de pruebas de estrés |

### Contenido del Informe

El informe técnico (`docs/informe.md`) contiene las siguientes secciones en el orden requerido:

1. Índice
2. Descripción detallada de los protocolos y aplicaciones desarrolladas
3. Problemas encontrados durante el diseño y la implementación
4. Limitaciones de la aplicación
5. Posibles extensiones
6. Conclusiones
7. Ejemplos de prueba
8. Guía de instalación detallada y precisa
9. Instrucciones para la configuración
10. Ejemplos de configuración y monitoreo
11. Documento de diseño del proyecto

---

## Requisitos del Sistema

### Sistema Operativo
- Linux (Ubuntu 20.04+, Debian 11+)
- macOS (Ventura 13.0+)

### Compilador
- GCC 4.9+ con soporte C11
- Clang 3.3+ (alternativa)

### Bibliotecas
- pthread (incluida en sistemas POSIX)
- Bibliotecas estándar de C

### Verificación de Requisitos

```bash
# Verificar versión del compilador
gcc --version

# Verificar soporte de C11
gcc -std=c11 --version

# Verificar GNU Make
make --version
```

---

## Procedimiento de Compilación

### Compilación Estándar

```bash
# 1. Clonar el repositorio (o extraer el tarball de entrega)
git clone <url-del-repositorio>
cd tpe-protos

# 2. Compilar ambos ejecutables
make

# 3. Verificar que se generaron los binarios
ls -la socks5d client
```

### Opciones de Compilación

```bash
# Compilar solo el servidor
make socks5d

# Compilar solo el cliente
make client

# Limpiar archivos compilados
make clean

# Recompilar desde cero
make clean && make
```

### Resultado Esperado

```
$ make
gcc -std=c11 -Wall -Wextra -pedantic -Iinclude -pthread -c -o build/buffer.o src/lib/buffer.c
gcc -std=c11 -Wall -Wextra -pedantic -Iinclude -pthread -c -o build/selector.o src/lib/selector.c
...
gcc -std=c11 -Wall -Wextra -pedantic -Iinclude -pthread -o socks5d build/buffer.o build/selector.o ...
gcc -std=c11 -Wall -Wextra -pedantic -Iinclude -pthread -o client build/client_main.o
```

---

## Artefactos Generados

Tras la compilación exitosa, se generan los siguientes artefactos:

| Artefacto | Ubicación | Descripción |
|-----------|-----------|-------------|
| `socks5d` | `./socks5d` | Servidor proxy SOCKS5 |
| `client` | `./client` | Cliente de gestión y monitoreo |
| `build/` | `./build/` | Archivos objeto intermedios (.o) |

### Verificación de Binarios

```bash
# Verificar que los binarios son ejecutables
file socks5d client

# Salida esperada:
# socks5d: Mach-O 64-bit executable arm64
# client:  Mach-O 64-bit executable arm64
```

---

## Ejecución del Servidor

### Sintaxis

```
./socks5d [OPCIONES]
```

### Opciones Disponibles

| Opción | Argumento | Descripción | Valor por Defecto |
|--------|-----------|-------------|-------------------|
| `-h` | - | Muestra ayuda y termina | - |
| `-v` | - | Muestra versión y termina | - |
| `-l` | `<dirección>` | Dirección de escucha para SOCKS | `0.0.0.0` |
| `-p` | `<puerto>` | Puerto para conexiones SOCKS | `1080` |
| `-L` | `<dirección>` | Dirección para gestión | `127.0.0.1` |
| `-P` | `<puerto>` | Puerto para gestión | `8080` |
| `-u` | `<user:pass>` | Usuario y contraseña (hasta 10) | Ninguno |
| `-N` | - | Desactiva disectores de protocolo | Activados |

### Ejemplos de Ejecución

```bash
# Servidor con un usuario
./socks5d -u admin:secretpass

# Servidor con múltiples usuarios
./socks5d -u admin:admin123 -u user1:pass1 -u user2:pass2

# Servidor en puertos personalizados
./socks5d -p 9050 -P 9051 -u admin:pass

# Servidor solo aceptando gestión desde localhost
./socks5d -L 127.0.0.1 -P 8080 -u admin:pass

# Servidor aceptando SOCKS desde cualquier interfaz
./socks5d -l 0.0.0.0 -p 1080 -u admin:pass

# Ver ayuda completa
./socks5d -h
```

### Salida del Servidor

```
$ ./socks5d -u admin:admin123 -u user1:pass1
[INFO] User added: admin
[INFO] User added: user1
[INFO] SOCKS5 server listening on 0.0.0.0:1080
[INFO] Management server listening on 127.0.0.1:8080
[INFO] Server started successfully. Waiting for connections...
```

---

## Ejecución del Cliente de Gestión

### Sintaxis

```
./client [OPCIONES]
```

### Opciones Disponibles

| Opción | Argumento | Descripción | Valor por Defecto |
|--------|-----------|-------------|-------------------|
| `-h` | - | Muestra ayuda | - |
| `-a` | `<dirección>` | Dirección del servidor | `127.0.0.1` |
| `-p` | `<puerto>` | Puerto de gestión | `8080` |

### Ejemplos de Ejecución

```bash
# Conectar a servidor local
./client

# Conectar a servidor remoto
./client -a 192.168.1.100 -p 8080

# Ver ayuda
./client -h
```

---

## Ejemplos de Uso

### Uso con curl

```bash
# Conexión HTTP a través del proxy
curl -x socks5h://admin:admin123@127.0.0.1:1080 http://example.com

# Conexión HTTPS a través del proxy
curl -x socks5h://admin:admin123@127.0.0.1:1080 https://www.google.com

# Verificar IP pública
curl -x socks5h://admin:admin123@127.0.0.1:1080 https://api.ipify.org

# Descargar archivo
curl -x socks5h://admin:admin123@127.0.0.1:1080 -O https://example.com/file.zip
```

> **Nota:** `socks5h://` indica que la resolución DNS debe realizarla el proxy.

### Uso con Firefox

1. Ir a `about:preferences`
2. Buscar "proxy" en el buscador
3. Click en "Settings..."
4. Seleccionar "Manual proxy configuration"
5. En "SOCKS Host": `127.0.0.1`, Port: `1080`
6. Seleccionar "SOCKS v5"
7. Marcar "Proxy DNS when using SOCKS v5"
8. Click "OK"

---

## Protocolo de Gestión

### Conexión

```bash
# Con netcat
nc 127.0.0.1 8080

# Con el cliente incluido
./client -a 127.0.0.1 -p 8080
```

### Comandos Disponibles

| Comando | Sintaxis | Descripción | Requiere Auth |
|---------|----------|-------------|---------------|
| `AUTH` | `AUTH <user> <pass>` | Autenticarse en el servidor | No |
| `STATS` | `STATS` | Mostrar métricas del servidor | Sí |
| `USERS` | `USERS` | Listar usuarios registrados | Sí |
| `ADDUSER` | `ADDUSER <user> <pass>` | Agregar usuario en runtime | Sí |
| `DELUSER` | `DELUSER <user>` | Eliminar usuario en runtime | Sí |
| `HELP` | `HELP` | Mostrar ayuda de comandos | Sí |
| `QUIT` | `QUIT` | Cerrar conexión | No |

### Sesión de Ejemplo

```
$ nc 127.0.0.1 8080
+OK SOCKS5 Management Server v1.0
+OK Use AUTH <user> <pass> to authenticate

AUTH admin admin123
+OK Authenticated successfully. Type HELP for commands.

STATS
+OK Statistics:
+OK   Total connections:    150
+OK   Current connections:  5
+OK   Bytes transferred:    1048576
+OK   Bytes sent:           524288
+OK   Bytes received:       524288
+OK   Successful:           145
+OK   Failed:               5

USERS
+OK Users (2 total):
+OK   USER admin
+OK   USER user1

ADDUSER guest guest123
+OK User 'guest' added successfully

DELUSER guest
+OK User 'guest' removed successfully

QUIT
+OK Bye
```

---

## Estructura del Proyecto

```
tpe-protos/
│
├── README.md                 # Este archivo
├── Makefile                  # Sistema de construcción
├── .gitignore                # Archivos ignorados por git
│
├── docs/                     # Documentación
│   ├── consigna.txt          # Enunciado del TP
│   ├── informe.md            # Informe técnico (Markdown)
│   ├── informe.html          # Informe técnico (HTML)
│   ├── stress_test.py        # Script de pruebas de estrés
│   ├── stress_report.md      # Resultados de pruebas
│   ├── args_referencia.c     # Código de referencia
│   ├── args_referencia.h
│   ├── main_referencia.c
│   └── socks5nio_referencia.c
│
├── include/                  # Headers públicos
│   ├── args.h                # Argumentos de línea de comandos
│   ├── buffer.h              # Buffer circular (cátedra)
│   ├── logger.h              # Sistema de logging
│   ├── metrics.h             # Métricas del servidor
│   ├── mgmt.h                # Protocolo de gestión
│   ├── netutils.h            # Utilidades de red (cátedra)
│   ├── parser.h              # Parser genérico (cátedra)
│   ├── selector.h            # Multiplexor I/O (cátedra)
│   ├── socks5nio.h           # Protocolo SOCKS5
│   ├── stm.h                 # Máquina de estados (cátedra)
│   └── users.h               # Gestión de usuarios
│
├── src/
│   ├── lib/                  # Framework de la cátedra
│   │   ├── buffer.c          # Buffer de I/O
│   │   ├── netutils.c        # Utilidades de red
│   │   ├── parser.c          # Parser genérico
│   │   ├── selector.c        # Multiplexor con select()
│   │   └── stm.c             # Máquina de estados finita
│   │
│   ├── server/               # Servidor SOCKS5
│   │   ├── main.c            # Punto de entrada
│   │   ├── args.c            # Parsing de argumentos
│   │   ├── logger.c          # Sistema de logging
│   │   ├── metrics.c         # Contadores atómicos
│   │   ├── mgmt.c            # Servidor de gestión
│   │   ├── socks5nio.c       # Implementación SOCKS5
│   │   └── users.c           # Gestión de usuarios
│   │
│   └── client/               # Cliente de gestión
│       └── main.c            # Cliente interactivo
│
├── patches/                  # Patches del framework original
│   └── *.patch
│
└── build/                    # Directorio de compilación (generado)
    └── *.o                   # Archivos objeto
```

---

## Cumplimiento de Requerimientos

| # | Requerimiento | Estado | Notas |
|---|---------------|--------|-------|
| 1 | 500+ conexiones simultáneas | OK | Probado con 1000+ |
| 2 | Autenticación RFC 1929 | OK | Usuario/contraseña obligatorio |
| 3 | CONNECT IPv4/IPv6/FQDN | OK | Todos los tipos soportados |
| 4 | Robustez múltiples IPs | OK | Fallback automático |
| 5 | Códigos de error completos | OK | Según RFC 1928 |
| 6 | Métricas de monitoreo | OK | Conexiones y bytes |
| 7 | Gestión en runtime | OK | ADDUSER/DELUSER |
| 8 | Registro de acceso | OK | Logs por conexión |

---

## Notas Adicionales

### Detener el Servidor

```bash
# Ctrl+C en la terminal donde se ejecuta
# O encontrar el PID y enviar SIGTERM
pkill socks5d
```

---
