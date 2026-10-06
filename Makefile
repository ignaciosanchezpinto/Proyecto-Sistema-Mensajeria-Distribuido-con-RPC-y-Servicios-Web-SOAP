# Compilador y banderas
CC = gcc
CFLAGS = -Wall -g -I/usr/include/tirpc
LDFLAGS = -ltirpc -lpthread

# Archivos generados por rpcgen
# El uso de -NC genera archivos compatibles con parámetros múltiples
RPC_GEN = log.h log_clnt.c log_svc.c

# Ejecutables finales
TARGETS = log_server server

# 1. Regla principal: Compila todo el proyecto
all: $(TARGETS)

# 2. Generación de stubs RPC a partir de la interfaz .x
# Se debe ejecutar rpcgen antes de compilar los fuentes
$(RPC_GEN): log.x
	rpcgen -NC log.x

# 3. Compilación del Servidor de Logs (RPC)
# Utiliza el servidor de rpcgen y la lógica implementada en log_server.c
log_server: log_server.c log_svc.c $(RPC_GEN)
	$(CC) $(CFLAGS) log_server.c log_svc.c -o log_server $(LDFLAGS)

# 4. Compilación del Servidor de Mensajería Principal
# Enlaza la lógica de datos, el cliente RPC y el servidor concurrente
server: server.c mensajeria.c log_clnt.c $(RPC_GEN)
	$(CC) $(CFLAGS) server.c mensajeria.c log_clnt.c -o server $(LDFLAGS)

# 5. Regla de limpieza
clean:
	rm -f $(TARGETS) $(RPC_GEN) *.o