/*
 * mensajeria.h — Cabecera del módulo de datos del servidor de mensajería
 *
 * Define las estructuras de datos, constantes y prototipos de las funciones
 * que gestionan usuarios y mensajes en memoria. Todas las funciones que
 * acceden a la lista compartida están protegidas internamente con un mutex.
 *
 */

#ifndef MENSAJERIA_H
#define MENSAJERIA_H

#include <pthread.h>
#include <netinet/in.h>

/* CONSTANTES DEL SISTEMA */

#define MAX_USERS    100   /* Número máximo de usuarios registrados simultáneamente */
#define MAX_MSG_LEN  256   /* Tamaño máximo de un mensaje en bytes (incluye el '\0') */
#define MAX_NAME_LEN 256   /* Tamaño máximo de un nombre de usuario (incluye '\0') */
#define MAX_USER_INFO_LEN (MAX_NAME_LEN + INET_ADDRSTRLEN + 15)

#define DESCONECTADO 0     /* Estado: el usuario no tiene sesión activa */
#define CONECTADO    1     /* Estado: el usuario está conectado y tiene puerto de escucha */

/*  ESTRUCTURAS DE DATO */

/*
 * Mensaje — Nodo de la lista enlazada de mensajes pendientes de entrega.
 *
 * Cuando el destinatario está desconectado, el servidor encola el mensaje
 * en esta lista. Se libera (free) cuando el mensaje se entrega con éxito
 * o cuando el usuario se da de baja del sistema.
 */
typedef struct Mensaje {
    unsigned int    id;                    /* Identificador único del mensaje (por remitente) */
    char            remitente[MAX_NAME_LEN]; /* Nombre del usuario que envió el mensaje */
    char            texto[MAX_MSG_LEN];    /* Contenido del mensaje (máx. 255 caracteres útiles) */
    char            archivo[MAX_NAME_LEN];   /* Nombre del adjunto */
    struct Mensaje *siguiente;             /* Puntero al siguiente nodo (lista enlazada simple) */
} Mensaje;

/*
 * Usuario — Entrada en la tabla de usuarios registrados.
 *
 * Se almacena en el array global lista_usuarios[]. Contiene el nombre,
 * el estado de conexión, los datos de red para contactar con el cliente,
 * el contador de id de mensajes y la lista de mensajes pendientes.
 */
typedef struct {
    char         nombre[MAX_NAME_LEN];     /* Alias único del usuario */
    int          estado;                   /* CONECTADO o DESCONECTADO */

    /* Datos de red del hilo de escucha del cliente (válidos solo si CONECTADO) */
    char         ip[INET_ADDRSTRLEN];      /* IP del cliente (obtenida con getpeername/accept) */
    int          puerto_escucha;           /* Puerto del hilo receptor del cliente */

    /* Control de identificadores de mensajes */
    unsigned int ultimo_id_mensaje;        /* Último id asignado a un mensaje de este usuario */

    /* Cola de mensajes pendientes de entrega (lista enlazada, asignada con malloc) */
    Mensaje     *mensajes_pendientes;      /* NULL si no hay mensajes en espera */
} Usuario;

/* VARIABLES GLOBALES (definidas en mensajeria.c)
 * Se exportan con extern para que server.c pueda acceder al mutex y a la
 * lista directamente en el caso de la entrega de mensajes pendientes al
 * reconectarse (sección 7.4 del enunciado).
 */
extern Usuario       lista_usuarios[MAX_USERS]; /* Tabla de usuarios registrados */
extern int           num_usuarios;              /* Número de usuarios actualmente registrados */
extern pthread_mutex_t mutex_usuarios;          /* Mutex que protege el acceso a la tabla */

/*  PROTOTIPOS DE FUNCIONES */

/* Inicializa el mutex y pone el contador de usuarios a 0. Debe llamarse una sola vez al inicio. */
void inicializar_datos(void);

/* Registra un nuevo usuario. Retorna 0=OK, 1=ya existe, 2=error (tabla llena). */
int registrar_usuario(char *nombre);

/* Elimina un usuario de la tabla y libera sus mensajes pendientes. Retorna 0=OK, 1=no existe. */
int dar_baja_usuario(char *nombre);

/* Marca a un usuario como conectado y guarda su IP y puerto. Retorna 0=OK, 1=no existe, 2=ya conectado. */
int conectar_usuario(char *nombre, char *ip, int puerto);

/* Marca a un usuario como desconectado y borra su IP/puerto. Retorna 0=OK, 1=no existe, 2=no estaba conectado. */
int desconectar_usuario(char *nombre);

/*
 * Obtiene la lista de usuarios conectados en ese momento.
 * Retorna 0=OK (el usuario existe y está conectado),
 *         1=el usuario no está conectado,
 *         2=el usuario no existe.
 * nombres_conectados y num_conectados se rellenan solo si retorna 0.
 */
int obtener_lista_usuarios(char *nombre,
                           char  nombres_conectados[][MAX_USER_INFO_LEN],
                           int  *num_conectados);

/*
 * Procesa un mensaje enviado por 'remitente' a 'destinatario':
 *   - Genera un id para el mensaje y lo asigna a *id_asignado.
 *   - Almacena SIEMPRE el mensaje en el buzón del destinatario.
 *   - Si el destinatario está conectado, rellena *dest_conectado=CONECTADO,
 *     ip_dest y *puerto_dest para que el servidor lo entregue en tiempo real.
 *   - Si está desconectado, *dest_conectado=DESCONECTADO (el mensaje queda en el buzón).
 * Retorna 0=OK, 1=alguno de los usuarios no existe, 2=error de asignación de memoria.
 */
int almacenar_mensaje(char        *remitente,
                      char        *destinatario,
                      char        *texto,
                      char         *archivo,  
                      unsigned int *id_asignado,
                      int          *dest_conectado,
                      char         *ip_dest,
                      int          *puerto_dest);

#endif /* MENSAJERIA_H */