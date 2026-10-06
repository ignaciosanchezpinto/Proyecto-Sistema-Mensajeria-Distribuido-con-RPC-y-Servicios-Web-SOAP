/*
 * server.c — Servidor concurrente multihilo del servicio de mensajería
 *
 * Escucha conexiones TCP de clientes y crea un hilo por cada una.
 * Cada hilo lee la operación solicitada y delega la lógica de datos
 * en las funciones de mensajeria.c (protegidas por mutex).
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netdb.h>       /* gethostname / gethostbyname para obtener la IP local real */
#include "mensajeria.h"
#include "log.h"
CLIENT *clnt_rpc; /* Cliente RPC global */
/* Mutex que serializa las llamadas RPC: clnt_create/clnt_call no son thread-safe */
static pthread_mutex_t mutex_rpc = PTHREAD_MUTEX_INITIALIZER;
/* FUNCIÓN AUXILIAR: leer_cadena
 * Lee del socket byte a byte hasta encontrar el terminador '\0'.
 * Reproduce el mismo comportamiento que _read_string en el cliente Python.
 *
 * @param socket   Descriptor del socket de donde leer.
 * @param buffer   Buffer de salida donde escribir los caracteres leídos.
 * @param max_len  Tamaño máximo del buffer (incluyendo el '\0' final).
 * @return Número de bytes escritos en buffer (sin contar el '\0'), o -1 si error.
 */
int leer_cadena(int socket_fd, char *buffer, int max_len) {
    /* Índice que recorre la cadena*/
    int  i = 0;
    /* Variable que usaremos para comprobar si el último carácter visitado es \0*/
    char c;

    /* Leemos del socket byte a byte hasta alcanzar el tope máximo de nuestro buffer */
    while (i < max_len - 1) {
        if (recv(socket_fd, &c, 1, 0) <= 0) return -1; /* Conexión cerrada o error */
        /*Añadimos al buffer el útimo caráter leído*/
        buffer[i++] = c;
        if (c == '\0') break; /* Terminador del protocolo encontrado */
    }
    buffer[i] = '\0'; /* Garantizamos terminación aunque no llegara '\0' */
    /*Devolvemos el número de bytes leídos del socket*/
    return i;
}

/*  FUNCIÓN: atender_cliente
 * Función que ejecuta cada hilo de atención a clientes.
 * Recibe el socket del cliente como argumento.
 * Lee la primera cadena para identificar la operación y la gestiona.
 *
 * Operaciones soportadas: REGISTER, UNREGISTER, CONNECT, DISCONNECT, USERS, SEND.
 */
void *atender_cliente(void *arg) {
    /* Recuperamos el descriptor del socket y liberamos el puntero temporal */
    int client_sock = *(int *)arg;
    free(arg);

    /*inicializamos las cadenas de operación, usuario y log_msg*/
    char operacion[256];
    char usuario[256];
    char log_msg[1024];
    char *p_msg = log_msg;
    /* Leer la operación solicitada por el cliente */
    if (leer_cadena(client_sock, operacion, sizeof(operacion)) <= 0) {
        close(client_sock);
        pthread_exit(NULL);
    }

    /* ── REGISTER ── */
    if (strcmp(operacion, "REGISTER") == 0) {
        if (leer_cadena(client_sock, usuario, sizeof(usuario)) > 0) {
            /* Llamamos a la función registrar_usuario creada en mesajeria.c*/
            int res = registrar_usuario(usuario);
            /*Tranformamos el código de operación en unsigned char para poder enviarlo*/
            unsigned char respuesta = (unsigned char)res;
            /*Enviamos al cliente la respuesta*/
            send(client_sock, &respuesta, 1, 0);
            /* Notificar al servidor RPC: "usuario REGISTER" */
            snprintf(log_msg, sizeof(log_msg), "%s REGISTER", usuario);
            /* Protegemos la llamada RPC con mutex porque clnt_call no es thread-safe */
            pthread_mutex_lock(&mutex_rpc);
            print_log_1(p_msg, clnt_rpc);
            pthread_mutex_unlock(&mutex_rpc);
        }
    }

    /* ── UNREGISTER ── */
    else if (strcmp(operacion, "UNREGISTER") == 0) {
        if (leer_cadena(client_sock, usuario, sizeof(usuario)) > 0) {
            /* Llamamos a la función dar_baja_usuario creada en mesajeria.c*/
            int res = dar_baja_usuario(usuario);
            /*Tranformamos el código de operación en unsigned char para poder enviarlo*/
            unsigned char respuesta = (unsigned char)res;
            /*Enviamos al cliente la respuesta*/
            send(client_sock, &respuesta, 1, 0);
            /* Notificar al servidor RPC: "usuario UNREGISTER" (sección 4) */
            snprintf(log_msg, sizeof(log_msg), "%s UNREGISTER", usuario);
            /* Protegemos la llamada RPC con mutex porque clnt_call no es thread-safe */
            pthread_mutex_lock(&mutex_rpc);
            print_log_1(p_msg, clnt_rpc);
            pthread_mutex_unlock(&mutex_rpc);
        }
    }

    /* ── CONNECT ── */
    else if (strcmp(operacion, "CONNECT") == 0) {
        /*Inicializamos la variable donde se encontrará el puerto*/
        char puerto_str[256];

        /* Leemos los datos recibidos*/
        if (leer_cadena(client_sock, usuario,    sizeof(usuario))    > 0 &&
            leer_cadena(client_sock, puerto_str, sizeof(puerto_str)) > 0) {

            /*Tranformamos el puerto recibido a entero*/
            int puerto_escucha = atoi(puerto_str);

            /* Obtener la IP del cliente a partir de la conexión TCP actual */
            struct sockaddr_in peer_addr;
            socklen_t peer_len = sizeof(peer_addr);
            getpeername(client_sock, (struct sockaddr *)&peer_addr, &peer_len);
            char *ip_cliente = inet_ntoa(peer_addr.sin_addr);

            /* Registrar la conexión en la tabla de usuarios */
            /* Llamamos a la función conectar_usuario creada en mesajeria.c*/
            int res = conectar_usuario(usuario, ip_cliente, puerto_escucha);
            /*Tranformamos el código de operación en unsigned char para poder enviarlo*/
            unsigned char respuesta = (unsigned char)res;
            /*Enviamos al cliente la respuesta*/
            send(client_sock, &respuesta, 1, 0);
            /* Notificar al servidor RPC: "usuario CONNECT" */
            snprintf(log_msg, sizeof(log_msg), "%s CONNECT", usuario);
            /* Protegemos la llamada RPC con mutex porque clnt_call no es thread-safe */
            pthread_mutex_lock(&mutex_rpc);
            print_log_1(p_msg, clnt_rpc);
            pthread_mutex_unlock(&mutex_rpc);

            /* Si la conexión tuvo éxito, entregar los mensajes pendientes*/
            if (res == 0) {
                pthread_mutex_lock(&mutex_usuarios); /* Iniciamos sesión crítica*/

                /* Buscar al usuario recién conectado en la tabla */
                int idx = -1;
                for (int i = 0; i < num_usuarios; i++) {
                    if (strcmp(lista_usuarios[i].nombre, usuario) == 0) {
                        idx = i;
                        break;
                    }
                }

                if (idx != -1) {
                    /* Caso de encontrar al usuario*/
                    /* Recorrer la lista enlazada de mensajes pendientes y entregarlos uno a uno */
                    Mensaje *actual   = lista_usuarios[idx].mensajes_pendientes;
                    Mensaje *anterior = NULL; /* Para poder retirar nodos de la lista */

                    while (actual != NULL) {
                        /*Bucle que se repite mientras haya mensajes que recibir*/
                        /* Abrir una nueva conexión al hilo receptor del cliente */
                        int s_p = socket(AF_INET, SOCK_STREAM, 0);
                        struct sockaddr_in addr_p;
                        addr_p.sin_family = AF_INET;
                        addr_p.sin_port   = htons(lista_usuarios[idx].puerto_escucha);
                        inet_pton(AF_INET, lista_usuarios[idx].ip, &addr_p.sin_addr);

                        /* Guardamos el id en un formato valido para enviarse*/
                        if (connect(s_p, (struct sockaddr *)&addr_p, sizeof(addr_p)) == 0) {
                            char id_s[20];
                            sprintf(id_s, "%u", actual->id);

                            if (strlen(actual->archivo) > 0) { /* Caso mensaje con adjunto */
                                /* Usamos strlen+1 en lugar de tamaño literal para evitar errores si el nombre cambia */
                                send(s_p, "SEND_MESSAGE_ATTACH", strlen("SEND_MESSAGE_ATTACH") + 1, 0);
                                /*Enviamos todos los datos del mensaje*/
                                send(s_p, actual->remitente, strlen(actual->remitente) + 1, 0);
                                send(s_p, id_s, strlen(id_s) + 1, 0);
                                send(s_p, actual->texto, strlen(actual->texto) + 1, 0);
                                send(s_p, actual->archivo, strlen(actual->archivo) + 1, 0);
                            } else { /* Caso mensaje normal */
                                /* Usamos strlen+1 en lugar de tamaño literal para evitar errores si el nombre cambia */
                                send(s_p, "SEND_MESSAGE", strlen("SEND_MESSAGE") + 1, 0);
                                /*Enviamos todos los datos del mensaje*/
                                send(s_p, actual->remitente, strlen(actual->remitente) + 1, 0);
                                send(s_p, id_s, strlen(id_s) + 1, 0);
                                send(s_p, actual->texto, strlen(actual->texto) + 1, 0);
                            }

                            /* Imprimimos la confirmación del envío del mensaje*/
                            printf("s> SEND MESSAGE %u FROM %s TO %s OK\n",
                                   actual->id, actual->remitente, usuario);

                            /* Cerramos socket temporal*/
                            close(s_p);

                            /* Entrega exitosa, retiramos el nodo del "buzón" y liberamos su memoria */
                            Mensaje *a_borrar = actual;
                            actual = actual->siguiente;

                            if (anterior == NULL) {
                                /* Caso primer nodo */
                                lista_usuarios[idx].mensajes_pendientes = actual;
                            } else {
                                /* Otros casos*/
                                anterior->siguiente = actual;
                            }
                            /*Libremos la memoría dinámica del mensaje*/
                            free(a_borrar);

                        } else {
                            /* Error de conexión: dejar el mensaje en el "buzón" 
                             * y marcar al usuario como desconectado*/
                            close(s_p);
                            lista_usuarios[idx].estado = DESCONECTADO;
                            lista_usuarios[idx].ip[0]  = '\0';
                            lista_usuarios[idx].puerto_escucha = 0;
                            printf("s> CONNECT %s FAIL (error delivering pending message)\n", usuario);
                            break; /* Detenemos la entrega; se reintentará en el próximo CONNECT */
                        }
                    }
                }

                pthread_mutex_unlock(&mutex_usuarios); /* Fin sección crítica*/
            }
        }
    }

    /* ── DISCONNECT ── */
    else if (strcmp(operacion, "DISCONNECT") == 0) {
        if (leer_cadena(client_sock, usuario, sizeof(usuario)) > 0) {
            /* Llamamos a la función desconectar_usuario creada en mesajeria.c*/
            int res = desconectar_usuario(usuario);
            /*Tranformamos el código de operación en unsigned char para poder enviarlo*/
            unsigned char respuesta = (unsigned char)res;
            /*Enviamos al cliente la respuesta*/
            send(client_sock, &respuesta, 1, 0);
            /* Notificar al servidor RPC: "usuario DISCONNECT"*/
            snprintf(log_msg, sizeof(log_msg), "%s DISCONNECT", usuario);
            /* Protegemos la llamada RPC con mutex porque clnt_call no es thread-safe */
            pthread_mutex_lock(&mutex_rpc);
            print_log_1(p_msg, clnt_rpc);
            pthread_mutex_unlock(&mutex_rpc);
        }
    }

    /* ── USERS ── */
    else if (strcmp(operacion, "USERS") == 0) {
        if (leer_cadena(client_sock, usuario, sizeof(usuario)) > 0) {
            /* Inicializamos la cadena de nombres_conectados y el entero de cuantos usuarios hay conectados*/
            char nombres_conectados[MAX_USERS][MAX_USER_INFO_LEN];
            int  num_conectados = 0;
            /* Llamamos a la función obtener_lista_usuarios creada en mesajeria.c*/
            int res = obtener_lista_usuarios(usuario, nombres_conectados, &num_conectados);
            /*Tranformamos el código de operación en unsigned char para poder enviarlo*/
            unsigned char respuesta = (unsigned char)res;
            /*Enviamos al cliente la respuesta*/
            send(client_sock, &respuesta, 1, 0);
            /* Notificar al servidor RPC: "usuario USERS" (sección 4) */
            snprintf(log_msg, sizeof(log_msg), "%s USERS", usuario);
            /* Protegemos la llamada RPC con mutex porque clnt_call no es thread-safe */
            pthread_mutex_lock(&mutex_rpc);
            print_log_1(p_msg, clnt_rpc);
            pthread_mutex_unlock(&mutex_rpc);

            if (res == 0) {
                /* Caso en el que se ha realizado la operación correctamente*/
                /* Enviar el número de usuarios conectados como cadena */
                char num_str[32];
                sprintf(num_str, "%d", num_conectados);
                send(client_sock, num_str, strlen(num_str) + 1, 0);

                /* Enviar cada nombre de usuario conectado */
                for (int i = 0; i < num_conectados; i++) {
                    send(client_sock,
                         nombres_conectados[i],
                         strlen(nombres_conectados[i]) + 1, 0);
                }
            }
        }
    }

    /* ── SEND ── */
    else if (strcmp(operacion, "SEND") == 0) {
        /* Inicializamos las varibables de los campos que recibimos*/
        char remitente[MAX_NAME_LEN];
        char destino[MAX_NAME_LEN];
        char texto[MAX_MSG_LEN];

        /* Leer los tres campos del protocolo*/
        if (leer_cadena(client_sock, remitente, sizeof(remitente)) <= 0 ||
            leer_cadena(client_sock, destino,   sizeof(destino))   <= 0 ||
            leer_cadena(client_sock, texto,     sizeof(texto))     <= 0) {
            /* Cerramos el socket y el hilo en caso de fallo*/
            close(client_sock);
            pthread_exit(NULL);
        }
        /*Id del mensaje*/
        unsigned int id_msg       = 0;
        /* Marcador si conectado o desconectado*/
        int          dest_conectado = 0;
        /* Puerto destinatario*/
        int          puerto_d     = 0;
        /*IP destinatario*/
        char         ip_d[INET_ADDRSTRLEN];

        /* Notificar al servidor RPC: "usuario SEND" (sección 4) */
        snprintf(log_msg, sizeof(log_msg), "%s SEND", remitente);
        /* Protegemos la llamada RPC con mutex porque clnt_call no es thread-safe */
        pthread_mutex_lock(&mutex_rpc);
        print_log_1(p_msg, clnt_rpc);
        pthread_mutex_unlock(&mutex_rpc);

        /* Almacenar el mensaje en el "buzón" del destinatario, usamos la función
         almacenar_mensaje creada en mensajería.c*/
        int res = almacenar_mensaje(remitente, destino, texto,"",
                                    &id_msg, &dest_conectado, ip_d, &puerto_d);
        /*Tranformamos el código de operación en unsigned char para poder enviarlo*/
        unsigned char byte_res = (unsigned char)res;
        /* Enviar al cliente la respuesta*/
        send(client_sock, &byte_res, 1, 0);

        if (res == 0) {
            /* Caso operación haya sido realizada correctamente*/
            /* Enviar el id asignado al remitente */
            char id_str[20];
            sprintf(id_str, "%u", id_msg);
            send(client_sock, id_str, strlen(id_str) + 1, 0);

            /* Si el destinatario está conectado, intentar entrega en tiempo real */
            if (dest_conectado == CONECTADO) {
                /* Creación de socket temporal para envío mensaje*/
                int sock_d = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in addr_d;
                addr_d.sin_family = AF_INET;
                addr_d.sin_port   = htons(puerto_d);
                inet_pton(AF_INET, ip_d, &addr_d.sin_addr);

                if (connect(sock_d, (struct sockaddr *)&addr_d, sizeof(addr_d)) == 0) {
                    /* Entrega el mensaje al hilo receptor del destinatario */
                    /* Usamos strlen+1 en lugar de tamaño literal para evitar errores si el nombre cambia */
                    send(sock_d, "SEND_MESSAGE", strlen("SEND_MESSAGE") + 1, 0);
                    /* Envío de resto de datos del mensaje*/
                    send(sock_d, remitente, strlen(remitente) + 1, 0);
                    send(sock_d, id_str,   strlen(id_str)    + 1, 0);
                    send(sock_d, texto,    strlen(texto)      + 1, 0);
                    /* Cierre socket temporal*/
                    close(sock_d);

                    /*Impresión de que le mensaje se envío correctamente*/
                    printf("s> SEND MESSAGE %u FROM %s TO %s OK\n",
                           id_msg, remitente, destino);

                    /* Entrega exitosa, borrar el mensaje del "buzón" del destinatario */
                    pthread_mutex_lock(&mutex_usuarios); /* Iicio sección crítica*/
                    for (int i = 0; i < num_usuarios; i++) {
                        if (strcmp(lista_usuarios[i].nombre, destino) == 0) {
                            /* Buscar el nodo con el id que acabamos de entregar */
                            Mensaje *anterior = NULL;
                            Mensaje *actual   = lista_usuarios[i].mensajes_pendientes;
                            while (actual != NULL) {
                                if (actual->id == id_msg) {
                                    if (anterior == NULL) {
                                        /* Caso único mensaje en "buzón"*/
                                        lista_usuarios[i].mensajes_pendientes = actual->siguiente;
                                    } else {
                                        /* Otros casos*/
                                        anterior->siguiente = actual->siguiente;
                                    }
                                    /*Liberación de memoria dinámica del mensaje*/
                                    free(actual);
                                    break;
                                }
                                anterior = actual;
                                actual   = actual->siguiente;
                            }
                            break;
                        }
                    }
                    pthread_mutex_unlock(&mutex_usuarios); /* Fin sección crítica*/

                    /* Enviar ACK al remitente*/
                    pthread_mutex_lock(&mutex_usuarios); /* Inicio sección crítica*/
                    /* Inicializar índice de la posición del remitente en la lista */
                    int idx_r = -1;
                    for (int i = 0; i < num_usuarios; i++) {
                        if (strcmp(lista_usuarios[i].nombre, remitente) == 0) {
                            /* Remitente encontrado*/
                            idx_r = i;
                            break;
                        }
                    }

                    /* Solo enviamos ACK si el remitente sigue conectado */
                    if (idx_r != -1 && lista_usuarios[idx_r].estado == CONECTADO) {
                        /* Creamos socket temporal para envíar ACK*/
                        int sock_ack = socket(AF_INET, SOCK_STREAM, 0);
                        struct sockaddr_in addr_ack;
                        addr_ack.sin_family = AF_INET;
                        addr_ack.sin_port   = htons(lista_usuarios[idx_r].puerto_escucha);
                        inet_pton(AF_INET, lista_usuarios[idx_r].ip, &addr_ack.sin_addr);

                        if (connect(sock_ack, (struct sockaddr *)&addr_ack, sizeof(addr_ack)) == 0) {
                            /* Enviar en cado de estar conectado*/
                            send(sock_ack, "SEND_MESS_ACK\0", strlen("SEND_MESS_ACK") + 1, 0);
                            send(sock_ack, id_str, strlen(id_str) + 1, 0);
                        }
                        /* Cierre de socket temporal*/
                        close(sock_ack);
                    }
                    /* Cierre sección crítica*/
                    pthread_mutex_unlock(&mutex_usuarios);

                } else {
                    /* Error de conexión: el cliente se desconectó inesperadamente.
                     * Marcarlo como DESCONECTADO. El mensaje queda en el buzón. */
                    /*Cierre socket destinatario*/
                    close(sock_d);
                    pthread_mutex_lock(&mutex_usuarios); /* Inicio sección crítica*/
                    for (int i = 0; i < num_usuarios; i++) {
                        if (strcmp(lista_usuarios[i].nombre, destino) == 0) {
                            lista_usuarios[i].estado         = DESCONECTADO;
                            lista_usuarios[i].ip[0]          = '\0';
                            lista_usuarios[i].puerto_escucha = 0;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&mutex_usuarios); /* Fin sección crítica*/
                }
            }
            /* Si dest_conectado == DESCONECTADO, el mensaje ya está en el buzón;
             * se entregará cuando el destinatario haga CONNECT (sección 7.4). */
        }
    }
    /* ── SENDATTACH ── */
    else if (strcmp(operacion, "SENDATTACH") == 0) {
        /* Inicializamos las variables de los datos recibidos*/
        char remitente[MAX_NAME_LEN], destino[MAX_NAME_LEN];
        char texto[MAX_MSG_LEN], fichero[MAX_NAME_LEN];

        /* Leer los 4 campos del protocolo*/
        if (leer_cadena(client_sock, remitente, sizeof(remitente)) > 0 &&
            leer_cadena(client_sock, destino,   sizeof(destino))   > 0 &&
            leer_cadena(client_sock, texto,     sizeof(texto))     > 0 &&
            leer_cadena(client_sock, fichero,   sizeof(fichero))   > 0) {
            /*Id mensaje*/
            unsigned int id_msg = 0;
            /*Destino conectado o desconectado y puerto del destino*/
            int dest_conectado = 0, puerto_d = 0;
            /*IP del destino*/
            char ip_d[INET_ADDRSTRLEN];

            /* Notificar al servidor RPC: "usuario SENDATTACH fichero" (sección 4) */
            snprintf(log_msg, sizeof(log_msg), "%s SENDATTACH %s", remitente, fichero);
            /* Protegemos la llamada RPC con mutex porque clnt_call no es thread-safe */
            pthread_mutex_lock(&mutex_rpc);
            print_log_1(p_msg, clnt_rpc);
            pthread_mutex_unlock(&mutex_rpc);

            /*Llamamos a función almacenar_mensaje creada en mensajeria.c*/
            int res = almacenar_mensaje(remitente, destino, texto, fichero, 
                                        &id_msg, &dest_conectado, ip_d, &puerto_d);
            
            /*Tranformamos código de operación a unsigned char para poder enviar*/
            unsigned char byte_res = (unsigned char)res;
            /* Enviamos al cliente*/
            send(client_sock, &byte_res, 1, 0);

                        /* --- MODIFICACIÓN EN server.c--- */
            if (res == 0) {
                /* Caso operción exitosa*/
                /* Enviamos id del mensaje*/
                char id_str[20];
                sprintf(id_str, "%u", id_msg);
                send(client_sock, id_str, strlen(id_str) + 1, 0);


                if (dest_conectado == CONECTADO) {
                    /* Caso destino conectado*/
                    /* Creamos socket temporal*/
                    int sock_d = socket(AF_INET, SOCK_STREAM, 0);
                    struct sockaddr_in addr_d;
                    addr_d.sin_family = AF_INET;
                    addr_d.sin_port = htons(puerto_d);
                    inet_pton(AF_INET, ip_d, &addr_d.sin_addr);

                    if (connect(sock_d, (struct sockaddr *)&addr_d, sizeof(addr_d)) == 0) {
                        /* Entrega al destinatario: usamos strlen+1 en lugar de tamaño literal */
                        send(sock_d, "SEND_MESSAGE_ATTACH", strlen("SEND_MESSAGE_ATTACH") + 1, 0);
                        /* Enviamos resto de datos del mensaje*/
                        send(sock_d, remitente, strlen(remitente) + 1, 0);
                        send(sock_d, id_str, strlen(id_str) + 1, 0);
                        send(sock_d, texto, strlen(texto) + 1, 0);
                        send(sock_d, fichero, strlen(fichero) + 1, 0);
                        /* Cierre socket temporal*/
                        close(sock_d);

                        /* Entrega exitosa, borramos el mensaje del buzón del destinatario.
                         * Sin este paso el mensaje quedaría atrapado en el buzón para siempre. */
                        pthread_mutex_lock(&mutex_usuarios); /* Inicio sección crítica*/
                        for (int i = 0; i < num_usuarios; i++) {
                            if (strcmp(lista_usuarios[i].nombre, destino) == 0) {
                                Mensaje *ant = NULL;
                                Mensaje *cur = lista_usuarios[i].mensajes_pendientes;
                                while (cur != NULL) {
                                    if (cur->id == id_msg) {
                                        if (ant == NULL)
                                            /*Caso mensajeúnico*/
                                            lista_usuarios[i].mensajes_pendientes = cur->siguiente;
                                        else
                                            /* Otros casos*/
                                            ant->siguiente = cur->siguiente;
                                        /* Liberamos memoria dinámica del mensaje*/
                                        free(cur);
                                        break;
                                    }
                                    ant = cur;
                                    cur = cur->siguiente;
                                }
                                break;
                            }
                        }
                        pthread_mutex_unlock(&mutex_usuarios); /* Fin sección crítica*/

                        /* Enviar ACK al remitente si sigue conectado.*/
                        pthread_mutex_lock(&mutex_usuarios); /* Inicio sección crítica*/
                        /* Inicializamos indice que indica la posición del remitente en lista_usuarios*/
                        int idx_rem = -1;
                        for (int i = 0; i < num_usuarios; i++) {
                            if (strcmp(lista_usuarios[i].nombre, remitente) == 0) {
                                /* Remitente encontrado*/
                                idx_rem = i; break;
                            }
                        }
                        /* Si el remitente sigue conectado, le enviamos el ACK del adjunto */
                        if (idx_rem != -1 && lista_usuarios[idx_rem].estado == CONECTADO) {
                            /* Creamos socket temporal*/
                            int s_ack = socket(AF_INET, SOCK_STREAM, 0);
                            struct sockaddr_in addr_ack;
                            addr_ack.sin_family = AF_INET;
                            addr_ack.sin_port = htons(lista_usuarios[idx_rem].puerto_escucha);
                            inet_pton(AF_INET, lista_usuarios[idx_rem].ip, &addr_ack.sin_addr);

                            if (connect(s_ack, (struct sockaddr *)&addr_ack, sizeof(addr_ack)) == 0) {
                                /* Usamos strlen+1 en lugar de tamaño literal */
                                send(s_ack, "SEND_MESS_ATTACH_ACK", strlen("SEND_MESS_ATTACH_ACK") + 1, 0);
                                /* Enviamos resto de datos del mensaje*/
                                send(s_ack, id_str, strlen(id_str) + 1, 0);
                                send(s_ack, fichero, strlen(fichero) + 1, 0);
                            }
                            /* Cerramos socket temporal*/
                            close(s_ack);
                        }
                        pthread_mutex_unlock(&mutex_usuarios); /* Fin sección crítica*/

                    } else {
                        /* Error de conexión al destinatario: el cliente se desconectó.
                         * Marcarlo como DESCONECTADO. El mensaje queda en el "buzón". */
                        /* Cierre socket temporal*/
                        close(sock_d);
                        pthread_mutex_lock(&mutex_usuarios); /* Inicio sección crítica*/
                        for (int i = 0; i < num_usuarios; i++) {
                            if (strcmp(lista_usuarios[i].nombre, destino) == 0) {
                                lista_usuarios[i].estado         = DESCONECTADO;
                                lista_usuarios[i].ip[0]          = '\0';
                                lista_usuarios[i].puerto_escucha = 0;
                                break;
                            }
                        }
                        pthread_mutex_unlock(&mutex_usuarios); /* Fin sección crítica*/
                    }
                }
            }
        }
    }
    
    
    /* Cerrar el socket de control una vez atendida la operación */
    close(client_sock);
    pthread_exit(NULL);
}

/* FUNCIÓN: main
 * Punto de entrada del servidor.
 * Parsea el puerto (-p), inicializa los datos, crea el socket de escucha
 * y entra en el bucle principal de aceptación de conexiones.
 * Por cada conexión entrante crea un hilo detached que la gestiona.
 */
int main(int argc, char *argv[]) {
    int port = 0;

    /* Parsear el argumento -p <puerto> */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[i + 1]);
        }
    }
    if (port == 0) {
        printf("Uso: ./server -p <port>\n");
        return 1;
    }

    /* Inicializar el mutex y la tabla de usuarios */
    inicializar_datos();

    /* Crear el socket de escucha TCP */
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return 1;
    }

    /* Permitir reutilizar el puerto inmediatamente tras cerrar el servidor */
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    listen(server_sock, 10);

    /* Obtener la IP local real con getsockname, en lugar de hardcodear 127.0.0.1.
     * Esto es correcto cuando el servidor corre en un contenedor con IP distinta. */
    struct sockaddr_in local_addr;
    socklen_t local_len = sizeof(local_addr);
    getsockname(server_sock, (struct sockaddr *)&local_addr, &local_len);
    char local_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local_addr.sin_addr, local_ip, sizeof(local_ip));
    /* Si la IP resultante es 0.0.0.0 (INADDR_ANY), la resolución por hostname es más útil */
    if (strcmp(local_ip, "0.0.0.0") == 0) {
        char hostname[256];
        gethostname(hostname, sizeof(hostname));
        struct hostent *he = gethostbyname(hostname);
        if (he != NULL)
            inet_ntop(AF_INET, he->h_addr_list[0], local_ip, sizeof(local_ip));
    }
    printf("s> init server %s:%d\n", local_ip, port);
    /* Leer la IP del servidor RPC de la variable de entorno LOG_RPC_IP  */
    char *rpc_ip = getenv("LOG_RPC_IP");
    if (rpc_ip == NULL) {
        fprintf(stderr, "s> ERROR: variable de entorno LOG_RPC_IP no definida\n");
        return 1;
    }
    /* Crear el cliente RPC conectado al servidor de log (protocolo UDP, como ONC-RPC) */
    clnt_rpc = clnt_create(rpc_ip, LOGPROG, LOGVERS, "udp");
    if (clnt_rpc == NULL) {
        clnt_pcreateerror(rpc_ip);
        return 1;
    }
    /* Bucle principal: aceptar conexiones y crear un hilo por cada una */
    while (1) {
        struct sockaddr_in c_addr;
        socklen_t c_len = sizeof(c_addr);

        /* Reservamos el descriptor en el heap para pasarlo al hilo de forma segura */
        int *c_sock = malloc(sizeof(int));
        if (!c_sock) continue; /* En caso de fallo de malloc, ignorar esta conexión */

        *c_sock = accept(server_sock, (struct sockaddr *)&c_addr, &c_len);
        if (*c_sock < 0) {
            free(c_sock);
            continue;
        }

        /* Crear el hilo de atención y desvincularlo (detach) para que libere
         * sus recursos automáticamente al terminar, sin necesitar pthread_join */
        pthread_t hilo;
        pthread_create(&hilo, NULL, atender_cliente, c_sock);
        pthread_detach(hilo);
    }

    return 0;
}