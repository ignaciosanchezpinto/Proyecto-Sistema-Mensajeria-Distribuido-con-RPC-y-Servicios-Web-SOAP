/*
 * mensajeria.c — Implementación del módulo de datos del servidor de mensajería
 *
 * Contiene la lógica de gestión de usuarios y mensajes en memoria.
 * Todas las secciones críticas que acceden a lista_usuarios[] están
 * protegidas con mutex_usuarios para garantizar la corrección en un
 * entorno multihilo (un hilo por cliente en server.c).
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mensajeria.h"

/*  VARIABLES GLOBALES (declaradas extern en mensajeria.h) */

Usuario        lista_usuarios[MAX_USERS]; /* Tabla de usuarios registrados */
int            num_usuarios = 0;          /* Número de usuarios actualmente en la tabla */
pthread_mutex_t mutex_usuarios;           /* Mutex que serializa el acceso a la tabla */

/* FUNCIÓN: inicializar_datos
 * Inicializa el mutex y pone el contador de usuarios a 0.
 * Debe llamarse una única vez al arrancar el servidor, antes de
 * aceptar cualquier conexión de cliente.
 */
void inicializar_datos(void) {
    pthread_mutex_init(&mutex_usuarios, NULL);
    num_usuarios = 0;
}

/* FUNCIÓN: registrar_usuario
 * Registra un nuevo usuario en la tabla si no existe ya otro con el mismo nombre.
 *
 * @param nombre  Nombre (alias) del usuario a registrar.
 * @return 0 = registrado con éxito
 *         1 = ya existe un usuario con ese nombre
 *         2 = tabla llena u otro error
 */
int registrar_usuario(char *nombre) {
    int resultado = 0; /* Asumimos éxito en primer lugar */

    pthread_mutex_lock(&mutex_usuarios); /* ── INICIO SECCIÓN CRÍTICA ── */

    /* 1. Comprobar si el nombre ya está en uso */
    for (int i = 0; i < num_usuarios; i++) {
        if (strcmp(lista_usuarios[i].nombre, nombre) == 0) {
            resultado = 1; /* Nombre duplicado */
            break;
        }
    }

    /* 2. Si no existe y hay espacio, añadir el nuevo usuario */
    if (resultado == 0) {
        if (num_usuarios < MAX_USERS) {
            /*Añadimos al usuario a la lita inicializando los datos de estado,
            ultimo_id_mensaje y mensajes_pendientes con datos por defecto*/
            strncpy(lista_usuarios[num_usuarios].nombre, nombre, MAX_NAME_LEN - 1);
            lista_usuarios[num_usuarios].nombre[MAX_NAME_LEN - 1] = '\0';
            lista_usuarios[num_usuarios].estado              = DESCONECTADO;
            lista_usuarios[num_usuarios].ultimo_id_mensaje   = 0;
            lista_usuarios[num_usuarios].mensajes_pendientes = NULL;
            /*La lista aumenta en uno*/
            num_usuarios++;
            /*Caso correcto*/
            printf("s> REGISTER %s OK\n", nombre);
        } else {
            resultado = 2; /* Tabla llena */
            printf("s> REGISTER %s FAIL\n", nombre);
        }
    } else {
        /*Caso en el que hay un fallo*/
        printf("s> REGISTER %s FAIL\n", nombre);
    }

    pthread_mutex_unlock(&mutex_usuarios); /* ── FIN SECCIÓN CRÍTICA ── */
    /*Devolvemos el código de operación correspodiente*/
    return resultado;
}

/* FUNCIÓN: dar_baja_usuario
 * Elimina un usuario de la tabla y libera su lista de mensajes pendientes.
 * Rellena el hueco dejado moviendo el último elemento de la tabla a esa posición.
 *
 * @param nombre  Nombre del usuario a eliminar.
 * @return 0 = eliminado con éxito
 *         1 = el usuario no existe
 */
int dar_baja_usuario(char *nombre) {
    int resultado = 1; /* Asumimos que el usuario no existe */

    pthread_mutex_lock(&mutex_usuarios); /* ── INICIO SECCIÓN CRÍTICA ── */

    /*Recorremos lista_usuarios e busca del nombre que se quiere dar de baja*/
    for (int i = 0; i < num_usuarios; i++) {
        if (strcmp(lista_usuarios[i].nombre, nombre) == 0) {

            /* En caso de encontral usuario liberamos la
             memoria dinámica de todos los mensajes pendientes */
            Mensaje *m = lista_usuarios[i].mensajes_pendientes;
            while (m != NULL) {
                Mensaje *temp = m;
                m = m->siguiente;
                free(temp); /* Cada nodo fue creado con malloc en almacenar_mensaje */
            }
            /*Desenlazamos al usuario de la lista enlazada*/
            lista_usuarios[i].mensajes_pendientes = NULL;

            /* Compactamos la tabla moviendo al último elemento a la posición liberada */
            if (i < num_usuarios - 1) {
                lista_usuarios[i] = lista_usuarios[num_usuarios - 1];
            }
            /*La lista disminuye en un usario*/
            num_usuarios--;
            /*Marcamos el código de operación correcto*/
            resultado = 0;
            break;
        }
    }

    if (resultado == 0) {
        /*Caso correcto*/
        printf("s> UNREGISTER %s OK\n", nombre);
    } else {
        /*Caso en el que hay algún fallo*/
        printf("s> UNREGISTER %s FAIL\n", nombre);
    }

    pthread_mutex_unlock(&mutex_usuarios); /* ── FIN SECCIÓN CRÍTICA ── */
    /*Devolvemos el código de operación*/
    return resultado;
}

/* FUNCIÓN: conectar_usuario
 * Marca al usuario como CONECTADO y guarda la IP y puerto de su hilo receptor.
 *
 * @param nombre  Nombre del usuario.
 * @param ip      IP del cliente (obtenida por el servidor con getpeername).
 * @param puerto  Puerto de escucha del hilo receptor del cliente.
 * @return 0 = conectado con éxito
 *         1 = el usuario no existe
 *         2 = el usuario ya estaba conectado
 */
int conectar_usuario(char *nombre, char *ip, int puerto) {
    int resultado = 1; /* Asumimos que el usuario no existe */

    pthread_mutex_lock(&mutex_usuarios); /* ── INICIO SECCIÓN CRÍTICA ── */

    /* Recorremos lista_usuario en busca del usuario que hay que conecta*/
    for (int i = 0; i < num_usuarios; i++) {
        if (strcmp(lista_usuarios[i].nombre, nombre) == 0) {
            /* Caso usuario encontrado*/

            if (lista_usuarios[i].estado == CONECTADO) {
                resultado = 2; /* Caso sesión ya conectada */
            } else {
                /* Caso sesión no conectada*/
                /* Guardamos datos de red y cambiar estado */
                lista_usuarios[i].estado = CONECTADO;
                strncpy(lista_usuarios[i].ip, ip, INET_ADDRSTRLEN - 1);
                lista_usuarios[i].ip[INET_ADDRSTRLEN - 1] = '\0';
                lista_usuarios[i].puerto_escucha = puerto;
                resultado = 0;
            }
            break;
        }
    }

    if (resultado == 0) {
        /* Caso correcto*/
        printf("s> CONNECT %s OK\n", nombre);
    } else {
        /* Caso en el que hay algún fallo*/
        printf("s> CONNECT %s FAIL\n", nombre);
    }

    pthread_mutex_unlock(&mutex_usuarios); /* ── FIN SECCIÓN CRÍTICA ── */
    /*Devolvemos el código de operación*/
    return resultado;
}

/* FUNCIÓN: desconectar_usuario
 * Marca al usuario como DESCONECTADO y borra su IP y puerto de escucha.
 *
 * @param nombre  Nombre del usuario.
 * @return 0 = desconectado con éxito
 *         1 = el usuario no existe
 *         2 = el usuario ya estaba desconectado
 */
int desconectar_usuario(char *nombre) {
    int resultado = 1; /* Asumimos que el usuario no existe */

    pthread_mutex_lock(&mutex_usuarios); /* ── INICIO SECCIÓN CRÍTICA ── */

    /*Recorremos la lista en búsqueda del usuario que desconectar*/
    for (int i = 0; i < num_usuarios; i++) {
        if (strcmp(lista_usuarios[i].nombre, nombre) == 0) {

            /* Caso usuario encontrado*/
            if (lista_usuarios[i].estado == DESCONECTADO) {
                resultado = 2; /* Caso usuario ya descoectado */
            } else {
                /*Caso en el que el usuario está conectado*/
                /* Borrar datos de red y cambiar estado */
                lista_usuarios[i].ip[0]          = '\0';
                lista_usuarios[i].puerto_escucha = 0;
                lista_usuarios[i].estado         = DESCONECTADO;
                resultado = 0;
            }
            break;
        }
    }
    
    if (resultado == 0) {
        /* Caso correcto */
        printf("s> DISCONNECT %s OK\n", nombre);
    } else {
        /* Caso en el que hay algún fallo */
        printf("s> DISCONNECT %s FAIL\n", nombre);
    }

    pthread_mutex_unlock(&mutex_usuarios); /* ── FIN SECCIÓN CRÍTICA ── */
    /*Devolvemos el código de operación*/
    return resultado;
}

/* FUNCIÓN: obtener_lista_usuarios
 * Devuelve la lista de usuarios actualmente conectados.
 * El usuario que solicita la lista debe existir y estar conectado.
 *
 * @param nombre             Nombre del usuario que hace la petición.
 * @param nombres_conectados Array de salida con los nombres de los conectados.
 * @param num_conectados     Salida: número de usuarios conectados.
 * @return 0 = OK (lista rellena en nombres_conectados)
 *         1 = el usuario solicitante no está conectado
 *         2 = el usuario solicitante no existe
 */
int obtener_lista_usuarios(char *nombre,
                           char  nombres_conectados[][MAX_USER_INFO_LEN],
                           int  *num_conectados) {
    int resultado    = 2; /* Asumimos que el usuario no existe */
    *num_conectados  = 0; /* Inicializamos el número de usuarios conectados en 0*/

    pthread_mutex_lock(&mutex_usuarios); /* ── INICIO SECCIÓN CRÍTICA ── */

    /* Buscar al usuario solicitante y verificar su estado */
    for (int i = 0; i < num_usuarios; i++) {
        if (strcmp(lista_usuarios[i].nombre, nombre) == 0) {

            /*Caso usuario encontrado*/
            if (lista_usuarios[i].estado == DESCONECTADO) {
                resultado = 1; /* Caso usuario desconectado */
            } else {
                /* Caso usuario conectado*/
                resultado = 0;
                /* Recopilar todos los usuarios conectados (incluido el solicitante) */
                for (int j = 0; j < num_usuarios; j++) {
                    if (lista_usuarios[j].estado == CONECTADO) {
                        /* Caso en el que usuario esté conectado*/
                        /* Formato exacto: usuario :: IP :: puerto */
                        /* Añadimos la información a nombres_conectados y aumentamos
                        en 1 num_conectados*/
                        snprintf(nombres_conectados[*num_conectados], MAX_USER_INFO_LEN, 
                                "%s :: %s :: %d", 
                                lista_usuarios[j].nombre, 
                                lista_usuarios[j].ip, 
                                lista_usuarios[j].puerto_escucha);
                        (*num_conectados)++;
                    }
                }
            }
            break;
        }
    }

    if (resultado == 0) {
        /* Caso correcto*/
        printf("s> CONNECTEDUSERS OK\n");
    } else {
        /*Caso en el que hay algún fallo*/
        printf("s> CONNECTEDUSERS FAIL\n");
    }

    pthread_mutex_unlock(&mutex_usuarios); /* ── FIN SECCIÓN CRÍTICA ── */
    /* Devolvemos código de operación*/
    return resultado;
}

/* FUNCIÓN: almacenar_mensaje
 * 
 * Procesa un mensaje enviado de 'remitente' a 'destinatario':
 *   1. Verifica que ambos usuarios existen.
 *   2. Genera el id del mensaje (contador del remitente + 1).
 *   3. Siempre encola el mensaje en el "buzón" del destinatario .
 *      almacenamos el mensaje independientemente de si el destino está conectado o no.
 *   4. Si el destinatario está conectado, rellena ip_dest y puerto_dest
 *      para que server.c lo entregue en tiempo real y luego lo borre del "buzón".
 *
 * @param remitente     Nombre del usuario que envía.
 * @param destinatario  Nombre del usuario que recibe.
 * @param texto         Contenido del mensaje.
 * @param id_asignado   Salida: id asignado al mensaje.
 * @param dest_conectado Salida: CONECTADO o DESCONECTADO.
 * @param ip_dest       Salida: IP del hilo receptor del destinatario (si conectado).
 * @param puerto_dest   Salida: puerto del hilo receptor del destinatario (si conectado).
 * @return 0 = OK
 *         1 = alguno de los usuarios no existe
 *         2 = error de asignación de memoria (malloc)
 */
int almacenar_mensaje(char        *remitente,
                      char        *destinatario,
                      char        *texto,
                      char         *archivo,  
                      unsigned int *id_asignado,
                      int          *dest_conectado,
                      char         *ip_dest,
                      int          *puerto_dest) {
    /* Inicializamos con -1 las posiciones en lista_usuario del remitente y del destinatario
    porque no sabemos todaía su posición o si existen*/
    int idx_rem  = -1;
    int idx_dest = -1;

    pthread_mutex_lock(&mutex_usuarios); /* ── INICIO SECCIÓN CRÍTICA ── */

    /* 1. Localizar a los dos usuarios en la tabla */
    for (int i = 0; i < num_usuarios; i++) {
        /* Si encontramos a cualquiera de los 2 añadir la posición al índice correspondiente*/
        if (strcmp(lista_usuarios[i].nombre, remitente)    == 0) idx_rem  = i;
        if (strcmp(lista_usuarios[i].nombre, destinatario) == 0) idx_dest = i;
    }

    /* Si alguno no existe, error inmediato */
    if (idx_rem == -1 || idx_dest == -1) {
        pthread_mutex_unlock(&mutex_usuarios); /* Liberación de mutex en caso de fallo.*/
        return 1; /*Devolvemos código de operación de fallo */
    }

    /* 2. Generar el id del mensaje (el contador del remitente se incrementa siempre) */
    lista_usuarios[idx_rem].ultimo_id_mensaje++;
    /* Protección contra desbordamiento*/
    if (lista_usuarios[idx_rem].ultimo_id_mensaje == 0) {
        lista_usuarios[idx_rem].ultimo_id_mensaje = 1;
    }
    /* Asignamos id correspondiente*/
    *id_asignado = lista_usuarios[idx_rem].ultimo_id_mensaje;

    /* 3. Crear el nodo del mensaje y encolarlo en el "buzón" del destinatario.
     *    siempre se almacena, independientemente del estado de conexión.
     *    El servidor lo entregará en tiempo real si está conectado y lo
     *    borrará del buzón solo cuando la entrega tenga éxito. */
    /* Reservamos memoria dinámica necesaria. para el Mensaje*/
    Mensaje *nuevo = (Mensaje *)malloc(sizeof(Mensaje));
    if (nuevo == NULL) {
        /* Fallo de asignación de memoria: error crítico */
        pthread_mutex_unlock(&mutex_usuarios); /* Liberación de mutex en caso de fallo.*/
        return 2; /* Devolvemos código de operación de fallo*/
    }
    /*Guardamos en la memoria dinámica reservada el nodo que contiene
    la información dle mensaje que hay que nviar*/
    nuevo->id        = *id_asignado;
    nuevo->siguiente = NULL;
    strncpy(nuevo->remitente, remitente, MAX_NAME_LEN - 1);
    nuevo->remitente[MAX_NAME_LEN - 1] = '\0';
    strncpy(nuevo->texto, texto, MAX_MSG_LEN - 1);
    nuevo->texto[MAX_MSG_LEN - 1] = '\0';
    strncpy(nuevo->archivo, archivo, MAX_NAME_LEN - 1);
    nuevo->archivo[MAX_NAME_LEN - 1] = '\0';

    /* Insertar al final de la lista enlazada del destinatario */
    if (lista_usuarios[idx_dest].mensajes_pendientes == NULL) {
        /* Caso en el que el "buzón" está vacío*/
        lista_usuarios[idx_dest].mensajes_pendientes = nuevo;
    } else {
        /*Caso en el que el "buzón" tiene mensajes*/
        Mensaje *aux = lista_usuarios[idx_dest].mensajes_pendientes;
        while (aux->siguiente != NULL) aux = aux->siguiente;
        aux->siguiente = nuevo;
    }

    /* 4. Comprobar si el destinatario está conectado y, si es así,
     *    devolver sus datos de red para entrega en tiempo real */
    *dest_conectado = lista_usuarios[idx_dest].estado;

    if (*dest_conectado == CONECTADO) {
        /* Pasamos la IP y puerto al llamador para entrega inmediata */
        strncpy(ip_dest, lista_usuarios[idx_dest].ip, INET_ADDRSTRLEN - 1);
        ip_dest[INET_ADDRSTRLEN - 1] = '\0';
        *puerto_dest = lista_usuarios[idx_dest].puerto_escucha;
        /* El mensaje se mostrará como "SEND MESSAGE ... OK" en server.c
         * cuando la entrega haya tenido éxito */
    } else {
        /* Si el destinatario desconectado el mensaje queda en el buzón hasta que se conecte */
        printf("s> MESSAGE %u FROM %s TO %s STORED\n",
               *id_asignado, remitente, destinatario);
    }

    pthread_mutex_unlock(&mutex_usuarios); /* ── FIN SECCIÓN CRÍTICA ── */
    return 0; /* Devolvemos código de operación correcto*/
}