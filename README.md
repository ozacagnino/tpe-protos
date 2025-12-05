# Trabajo Práctico Especial - Protocolos de Comunicación 2025/2

## Servidor Proxy SOCKSv5

Implementación completa de un servidor proxy SOCKSv5 concurrente con autenticación usuario/contraseña y un protocolo de gestión/monitoreo.

---

## Compilación

```bash
make        # Compila servidor y cliente
make clean  # Limpia archivos compilados
```

## Ejecución

### Servidor SOCKS5

```bash
./socks5d -u usuario:password
./socks5d -u admin:admin123 -u user1:pass1 -p 1080 -P 8080
```

### Cliente de gestión

```bash
./client -L 127.0.0.1 -P 8080 -u admin -p admin123
```

## Pruebas

### Proxy SOCKS5

```bash
curl -x socks5h://usuario:password@127.0.0.1:1080 https://www.google.com
curl -x socks5h://usuario:password@127.0.0.1:1080 https://api.ipify.org
```

### Protocolo de Gestión

```bash
nc 127.0.0.1 8080
AUTH admin admin123
STATS
USERS
ADDUSER nuevo pass123
QUIT
```

## Arquitectura

- **Single-threaded event loop** con selector.c
- **I/O no bloqueante** en todas las operaciones
- **Máquina de estados** para SOCKS5 y gestión
- **Resolución DNS** en thread separado

## Cumplimiento de Requerimientos

| Requerimiento | Estado |
|--------------|--------|
| Múltiples clientes (500+) | ✅ |
| Auth usuario/contraseña | ✅ |
| CONNECT IPv4/IPv6/FQDN | ✅ |
| Robustez múltiples IPs | ✅ |
| Métricas | ✅ |
| Gestión runtime | ✅ |
| Logging | ✅ |

## Estructura

```
tpe-protos/
├── Makefile
├── README.md
├── docs/           # Consigna y referencias
├── include/        # Headers
├── patches/        # Framework de la cátedra
└── src/
    ├── client/     # Cliente de gestión
    ├── lib/        # Framework (selector, buffer, stm)
    └── server/     # Servidor SOCKS5 y gestión
```

---

ITBA - Protocolos de Comunicación 2025/2
