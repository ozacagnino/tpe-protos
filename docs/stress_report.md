# Informe de Pruebas de Estrés - Servidor SOCKS5

## Resumen Ejecutivo

El servidor SOCKS5 fue sometido a pruebas de estrés para evaluar:
1. **Máxima cantidad de conexiones simultáneas**
2. **Degradación del throughput bajo carga**

## Entorno de Pruebas

- **Sistema Operativo:** macOS Darwin 24.6.0
- **File Descriptors disponibles:** 1,048,575
- **CPU:** Apple Silicon
- **RAM:** Estándar del sistema

## Resultados de las Pruebas

### 1. Conexiones Simultáneas

| Conexiones | Exitosas | Fallidas | Tiempo | Rate (conn/s) |
|------------|----------|----------|--------|---------------|
| 10         | 10       | 0        | 0.11s  | 93.6          |
| 50         | 50       | 0        | 0.11s  | 437.8         |
| 100        | 100      | 0        | 0.13s  | 784.6         |
| 200        | 200      | 0        | 0.15s  | 1,369.8       |
| 500        | 500      | 0        | 0.19s  | 2,630.9       |
| 750        | 750      | 0        | 0.22s  | 3,400.2       |
| 1,000      | 1,000    | 0        | 0.26s  | 3,887.0       |
| 1,500      | 1,018    | 482      | 0.15s  | 6,745.4       |

**Conclusión:** El servidor soporta hasta **1,000 conexiones simultáneas** sin degradación. 
A partir de 1,500 conexiones se observa degradación debido al límite de FD_SETSIZE (1024) 
del multiplexor select().

### 2. Tiempos de Respuesta

| Conexiones | Promedio | Mínimo  | Máximo  | Desv. Std |
|------------|----------|---------|---------|-----------|
| 10         | 0.61ms   | 0.29ms  | 0.94ms  | 0.20ms    |
| 100        | 2.10ms   | 0.57ms  | 5.41ms  | 0.86ms    |
| 500        | 1.52ms   | 0.27ms  | 7.02ms  | 1.33ms    |
| 1,000      | 2.08ms   | 0.30ms  | 12.90ms | 2.49ms    |

**Conclusión:** Los tiempos de respuesta se mantienen estables (< 3ms promedio) 
hasta 1,000 conexiones. El máximo ocasional aumenta con la carga pero se mantiene 
dentro de límites aceptables (< 15ms).

### 3. Throughput Sostenido

Con 50 workers durante 5 segundos:
- **Total conexiones:** 2,434
- **Throughput:** 486.8 conexiones/segundo
- **Tasa de éxito:** 100%

### 4. Límites del Sistema

| Métrica | Valor |
|---------|-------|
| Conexiones simultáneas máximas | ~1,018 |
| Throughput máximo (burst) | 3,887 conn/s |
| Throughput sostenido | ~487 conn/s |
| Tiempo de respuesta promedio | < 3ms |

## Análisis de Degradación

La degradación observada a partir de ~1,000 conexiones se debe a:

1. **FD_SETSIZE:** El multiplexor `select()` tiene un límite de 1024 file descriptors 
   en la mayoría de sistemas POSIX.

2. **Arquitectura single-threaded:** El event loop procesa todas las conexiones 
   secuencialmente, lo cual es eficiente hasta cierto punto.

3. **Recursos del sistema:** Cada conexión consume memoria para buffers y estructuras 
   de estado.

## Cumplimiento de Requerimientos

| Requerimiento | Estado | Evidencia |
|---------------|--------|-----------|
| 500+ conexiones concurrentes | ✅ CUMPLE | 1,000 conexiones sin fallos |
| Performance aceptable | ✅ CUMPLE | < 3ms tiempo de respuesta |
| Estabilidad bajo carga | ✅ CUMPLE | 0% fallos hasta 1,000 conn |

## Recomendaciones para Producción

1. Usar `epoll` (Linux) o `kqueue` (BSD/macOS) para superar límite de select()
2. Implementar pooling de conexiones si se esperan más de 1,000 clientes
3. Ajustar ulimit si es necesario soportar más conexiones

## Conclusión

El servidor **cumple y excede** el requerimiento de soportar al menos 500 conexiones 
simultáneas, alcanzando **1,000 conexiones** con 0% de fallos y tiempos de respuesta 
inferiores a 3ms. El throughput sostenido de ~487 conn/s es adecuado para la mayoría 
de casos de uso de un proxy SOCKS5.
