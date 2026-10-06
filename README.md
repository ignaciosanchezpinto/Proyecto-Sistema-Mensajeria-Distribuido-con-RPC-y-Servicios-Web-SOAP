# Proyecto de Sistemas Distribuidos (SSDD) - Sistema de Mensajería

**Autores:** Ignacio (100522247) y Rodrigo (100526372)

Las instrucciones detalladas para la compilación, despliegue y ejecución del sistema de mensajería distribuido son descritas en este documento. En este proyecto, sockets TCP, llamadas a procedimientos remotos (RPC) y un servicio web SOAP son integrados de forma concurrente.

## 1. Requisitos Previos

Antes de que la ejecución sea iniciada, los siguientes componentes deben estar instalados en el entorno (el contenedor Docker `practica_u22` es recomendado):
*   **GCC y Make**: Para que el código en C sea compilado.
*   **rpcbind y rpcgen**: Para que el middleware RPC sea generado y ejecutado.
*   **Python 3**: Para que el cliente y el servicio SOAP sean lanzados.
*   **Zeep (Python)**: La librería SOAP requerida por el servicio web.

## 2. Compilación del Sistema

Un archivo `Makefile` ha sido provisto para que la construcción del proyecto sea automatizada mediante el uso de archivos objeto (`.o`). 

Para que los ejecutables sean generados, el siguiente comando debe ser introducido en la raíz del proyecto:

make
Una vez que el comando es ejecutado, los binarios server y log_server, junto con las cabeceras RPC (log.h, etc.), serán creados.

Para que los archivos binarios y temporales sean eliminados y el directorio sea limpiado antes de una entrega, este comando es utilizado:

Bash
make clean
3. Orden de Ejecución
Para que el correcto funcionamiento de las conexiones sea garantizado, los distintos servicios deben ser iniciados en el siguiente orden estricto (múltiples terminales son requeridas):

Paso 1: Iniciar el demonio RPC
El servicio de mapeo de puertos RPC debe ser arrancado primero.

Bash
service rpcbind start
Paso 2: Desplegar el Servidor de Logs (RPC)
El registro de auditoría es mantenido por este proceso.

Bash
./log_server
Paso 3: Desplegar el Servicio Web (SOAP)
El servicio de normalización de mensajes es iniciado mediante Python.

Bash
python3 web_service.py
(El puerto 8000 será escuchado por defecto).

Paso 4: Desplegar el Servidor Principal (C)
La variable de entorno con la IP del servidor RPC debe ser exportada antes de que el servidor sea lanzado.

Bash
export LOG_RPC_IP=127.0.0.1
./server -p 8080
Paso 5: Ejecutar el Cliente (Python)
Finalmente, las peticiones de los usuarios son enviadas a través del cliente.

Bash
python3 client.py -s 127.0.0.1 -p 8080
4. Estructura de Archivos
server.c, mensajeria.c, mensajeria.h: Por estos archivos es definida la lógica del servidor principal, el buzón de persistencia y el manejo de usuarios (Sockets y Threads).

client.py: La interfaz de usuario es provista por este script.

web_service.py: La limpieza de los mensajes mediante SOAP es realizada por este servicio.

log.x, log_server.c: La interfaz y la lógica del servidor de auditoría RPC son definidas aquí.

Makefile: Las directivas de compilación son contenidas en este archivo.
