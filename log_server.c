/*
 * log_server.c — Implementación del servidor RPC de registro de operaciones
 *
 * Este fichero implementa el procedimiento remoto PRINT_LOG definido en log.x.
 * El servidor de mensajería (server.c) actúa como cliente RPC y llama a este
 * procedimiento cada vez que un usuario realiza una operación, enviando una
 * cadena con el formato: "nombre_usuario OPERACION [fichero]"
 *
 * El servidor RPC imprime por pantalla cada operación recibida.
 * Usamos el modelo ONC-RPC con protocolo UDP .
 *
 * Compilación (tras ejecutar rpcgen log.x):
 *   gcc -o log_server log_server.c log_svc.c log_xdr.c -lpthread
 *
 * Ejecución:
 *   ./log_server
 *
 */

#include "log.h"   /* Cabecera generada automáticamente por rpcgen a partir de log.x */
#include <stdio.h>

/*
 * FUNCIÓN: print_log_1_svc
 *
 * Implementación del procedimiento remoto PRINT_LOG.
 * Es invocada por el stub del servidor generado por rpcgen cada vez que
 * server.c llama remotamente a print_log_1().
 *
 * El formato esperado del mensaje es:
 *   "nombre_usuario OPERACION"         para REGISTER, UNREGISTER, CONNECT,
 *                                       DISCONNECT, USERS, SEND
 *   "nombre_usuario SENDATTACH /ruta"  para SENDATTACH (incluye nombre del fichero)
 *
 * @param msg      Puntero al puntero de cadena recibida (convención ONC-RPC para strings).
 * @param req      Información de la petición RPC (no usada aquí).
 * @return         Puntero a entero estático con valor 0 (OK).
 */
#include "log.h"
#include <stdio.h>

/* Cambiamos char **msg por char *msg para que coincida con log.h */
int * print_log_1_svc(char *msg, struct svc_req *req) {
    static int result = 0;

    /* Si el mensaje es nulo, no hacemos nada */
    if (msg == NULL) {
        return &result;
    }

    /* Imprimimos el log */
    printf("%s\n", msg);

    return &result;
}