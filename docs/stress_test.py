#!/usr/bin/env python3
"""
Pruebas de Estrés para Servidor SOCKS5
======================================
Responde a las preguntas de la consigna:
1. ¿Cuál es la máxima cantidad de conexiones simultáneas que soporta?
2. ¿Cómo se degrada el throughput?
"""

import socket
import threading
import time
import sys
import statistics
from concurrent.futures import ThreadPoolExecutor, as_completed

SOCKS_HOST = '127.0.0.1'
SOCKS_PORT = 1080
USER = b'testuser'
PASS = b'testpass123'

# Contadores globales
successful_connections = 0
failed_connections = 0
connection_times = []
lock = threading.Lock()

def socks5_handshake(sock):
    """Realiza el handshake SOCKS5 completo"""
    # HELLO
    sock.send(b'\x05\x01\x02')
    resp = sock.recv(2)
    if resp != b'\x05\x02':
        return False
    
    # AUTH
    auth = bytes([0x01, len(USER)]) + USER + bytes([len(PASS)]) + PASS
    sock.send(auth)
    resp = sock.recv(2)
    if resp[1] != 0:
        return False
    
    return True

def make_connection(conn_id):
    """Realiza una conexión completa al proxy"""
    global successful_connections, failed_connections, connection_times
    
    start_time = time.time()
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(10)
        sock.connect((SOCKS_HOST, SOCKS_PORT))
        
        if socks5_handshake(sock):
            elapsed = time.time() - start_time
            with lock:
                successful_connections += 1
                connection_times.append(elapsed)
            # Mantener conexión por un momento
            time.sleep(0.1)
            sock.close()
            return True
        else:
            with lock:
                failed_connections += 1
            sock.close()
            return False
    except Exception as e:
        with lock:
            failed_connections += 1
        return False

def test_concurrent_connections(num_connections):
    """Prueba N conexiones concurrentes"""
    global successful_connections, failed_connections, connection_times
    successful_connections = 0
    failed_connections = 0
    connection_times = []
    
    print(f"\n{'='*60}")
    print(f"TEST: {num_connections} conexiones concurrentes")
    print('='*60)
    
    start = time.time()
    
    with ThreadPoolExecutor(max_workers=num_connections) as executor:
        futures = [executor.submit(make_connection, i) for i in range(num_connections)]
        for future in as_completed(futures):
            pass
    
    elapsed = time.time() - start
    
    print(f"  Conexiones exitosas:  {successful_connections}/{num_connections}")
    print(f"  Conexiones fallidas:  {failed_connections}")
    print(f"  Tiempo total:         {elapsed:.2f}s")
    print(f"  Conexiones/segundo:   {successful_connections/elapsed:.2f}")
    
    if connection_times:
        print(f"  Tiempo promedio:      {statistics.mean(connection_times)*1000:.2f}ms")
        print(f"  Tiempo mínimo:        {min(connection_times)*1000:.2f}ms")
        print(f"  Tiempo máximo:        {max(connection_times)*1000:.2f}ms")
        if len(connection_times) > 1:
            print(f"  Desviación estándar:  {statistics.stdev(connection_times)*1000:.2f}ms")
    
    return successful_connections, failed_connections, elapsed

def test_throughput(duration_seconds=5):
    """Prueba de throughput: cuántas conexiones por segundo"""
    global successful_connections, failed_connections
    successful_connections = 0
    failed_connections = 0
    
    print(f"\n{'='*60}")
    print(f"TEST: Throughput (duración: {duration_seconds}s)")
    print('='*60)
    
    stop_event = threading.Event()
    
    def worker():
        while not stop_event.is_set():
            make_connection(0)
    
    # Usar 50 workers
    threads = []
    for i in range(50):
        t = threading.Thread(target=worker)
        t.start()
        threads.append(t)
    
    time.sleep(duration_seconds)
    stop_event.set()
    
    for t in threads:
        t.join()
    
    print(f"  Total conexiones:     {successful_connections + failed_connections}")
    print(f"  Exitosas:             {successful_connections}")
    print(f"  Fallidas:             {failed_connections}")
    print(f"  Throughput:           {successful_connections/duration_seconds:.2f} conn/s")
    
    return successful_connections / duration_seconds

def main():
    print("╔══════════════════════════════════════════════════════════════╗")
    print("║         PRUEBAS DE ESTRÉS - SERVIDOR SOCKS5                 ║")
    print("╚══════════════════════════════════════════════════════════════╝")
    
    # Verificar que el servidor está corriendo
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2)
        sock.connect((SOCKS_HOST, SOCKS_PORT))
        sock.close()
        print(f"\n✓ Servidor detectado en {SOCKS_HOST}:{SOCKS_PORT}")
    except:
        print(f"\n✗ ERROR: Servidor no disponible en {SOCKS_HOST}:{SOCKS_PORT}")
        print("  Ejecuta: ./socks5d -u testuser:testpass123")
        sys.exit(1)
    
    results = []
    
    # Test 1: Conexiones concurrentes progresivas
    print("\n" + "═"*60)
    print("FASE 1: MÁXIMAS CONEXIONES SIMULTÁNEAS")
    print("═"*60)
    
    for n in [10, 50, 100, 200, 500, 750, 1000]:
        success, fail, elapsed = test_concurrent_connections(n)
        results.append({
            'connections': n,
            'success': success,
            'fail': fail,
            'time': elapsed,
            'rate': success/elapsed if elapsed > 0 else 0
        })
        time.sleep(1)  # Pausa entre tests
        
        # Si más del 50% falla, detenerse
        if fail > success:
            print(f"\n⚠ Más del 50% de conexiones fallaron. Deteniendo.")
            break
    
    # Test 2: Throughput sostenido
    print("\n" + "═"*60)
    print("FASE 2: THROUGHPUT SOSTENIDO")
    print("═"*60)
    
    throughput = test_throughput(5)
    
    # Resumen final
    print("\n" + "═"*60)
    print("RESUMEN DE RESULTADOS")
    print("═"*60)
    
    print("\n┌─────────────┬─────────┬─────────┬──────────┬────────────┐")
    print("│ Conexiones  │ Éxito   │ Fallo   │ Tiempo   │ Rate       │")
    print("├─────────────┼─────────┼─────────┼──────────┼────────────┤")
    for r in results:
        print(f"│ {r['connections']:>11} │ {r['success']:>7} │ {r['fail']:>7} │ {r['time']:>7.2f}s │ {r['rate']:>8.1f}/s │")
    print("└─────────────┴─────────┴─────────┴──────────┴────────────┘")
    
    print(f"\n📊 CONCLUSIONES:")
    max_success = max(results, key=lambda x: x['success'])
    print(f"   • Máx conexiones simultáneas exitosas: {max_success['success']}")
    print(f"   • Throughput sostenido: {throughput:.1f} conexiones/segundo")
    
    # Guardar resultados para el informe
    with open('stress_results.txt', 'w') as f:
        f.write("RESULTADOS PRUEBAS DE ESTRÉS\n")
        f.write("="*50 + "\n\n")
        f.write("1. CONEXIONES SIMULTÁNEAS\n")
        for r in results:
            f.write(f"   {r['connections']} conexiones: {r['success']} éxito, {r['fail']} fallo, {r['rate']:.1f}/s\n")
        f.write(f"\n2. THROUGHPUT SOSTENIDO: {throughput:.1f} conn/s\n")
        f.write(f"\n3. MÁXIMO ALCANZADO: {max_success['success']} conexiones simultáneas\n")
    
    print(f"\n✓ Resultados guardados en stress_results.txt")

if __name__ == '__main__':
    main()
