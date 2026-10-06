from enum import Enum
import argparse
import socket
import threading
import zeep #

WSDL_URL = "http://localhost:8000/?wsdl"
class client :
    
    # ******************** TYPES *********************
    # *
    # * @brief Return codes for the protocol methods
    class RC(Enum) :
        OK = 0
        ERROR = 1
        USER_ERROR = 2

    # ****************** ATTRIBUTES ******************
    _server = None
    _port = -1
    _listener_sock = None  # Socket de escucha del hilo receptor (se crea en CONNECT)
    _username = ""         # Nombre del usuario actualmente conectado en este terminal
    _user_table = {}       # Diccionario cuya clave es el nombre de usuario y donde guardamos la ip y el puerto
    # ******************** METHODS *******************

    # -------------------------------------------------------
    # Función auxiliar: lee del socket byte a byte hasta '\0'
    # Reproduce el comportamiento de las cadenas C del protocolo
    # -------------------------------------------------------
    @staticmethod
    def _read_string(sock):
        chars = []
        while True:
            b = sock.recv(1)
            # Si la conexión se cierra o llega el terminador nulo, paramos
            if not b or b == b'\0':
                break
            # añadimos la cadena codificada a chars para devolverla posteriormente
            chars.append(b.decode('utf-8'))
        return "".join(chars)
    
    # -------------------------------------------------------
    # Función auxiliar: Llama al Web Service SOAP para normalizar el texto.
    # Si el servicio falla o está caído, devuelve el texto original intacto.
    # -------------------------------------------------------
    @staticmethod
    def _normalize_message(texto):
        try:
            import logging
            # Silenciamos los logs de red para mantener la terminal del cliente limpia
            logging.getLogger('zeep').setLevel(logging.ERROR)
            logging.getLogger('urllib3').setLevel(logging.ERROR)
            
            ws_client = zeep.Client(wsdl=WSDL_URL)
            texto_limpio = ws_client.service.normalizar(texto)
            
            # Zeep podría devolver None si el servicio falla internamente
            if texto_limpio is not None:
                return texto_limpio
            return texto
            
        except (zeep.exceptions.Error, OSError, ValueError):
            # zeep.exceptions.Error: fallo SOAP (timeout, servicio caído, respuesta inválida)
            # OSError: error de red al conectar con el servicio web
            # ValueError: respuesta inesperada del servicio
            return texto

    # -------------------------------------------------------
    # Hilo secundario que escucha mensajes entrantes del servidor.
    # Se crea en CONNECT y muere cuando se cierra el socket en DISCONNECT.
    # Gestiona "SEND MESSAGE", "SEND MESS ACK", "SEND_MESSAGE_ATTACH", "SEND_MESS_ATTACH_ACK" Y "GET FILE".
    # -------------------------------------------------------
    @staticmethod
    def _listener_thread(sock):
        while True:
            try:
                # Creación del socket temporal para la comunicación
                conn, addr = sock.accept()
                op = client._read_string(conn)

                if op == "SEND_MESSAGE":
                    # En caso de "SEND_MESSAGE" recepción de remitente, id_msg y texto para posterior impresión
                    remitente = client._read_string(conn)
                    id_msg    = client._read_string(conn)
                    texto     = client._read_string(conn)
                    # prefijo "s>" para mensajes recibidos sin adjunto
                    # flush=True asegura que se vea en Docker inmediatamente
                    print(f"\ns> MESSAGE {id_msg} FROM {remitente}\n   {texto}\n   END", flush=True)
                    print("c> ", end="", flush=True) 

                elif op == "SEND_MESS_ACK":
                     # En caso de "SEND_MESS_ACK" recepción de id_msg para posterior impresión
                    id_msg = client._read_string(conn)
                    # flush=True asegura que se vea en Docker inmediatamente
                    print(f"\nc> SEND MESSAGE {id_msg} OK", flush=True)
                    print("c> ", end="", flush=True)

                elif op == "SEND_MESSAGE_ATTACH":
                    # En caso de "SEND_MESSAGE_ATTACH" recepción de remitente, id_msg, texto y fichero para posterior impresión
                    remitente = client._read_string(conn)
                    id_msg = client._read_string(conn)
                    texto = client._read_string(conn)
                    fichero = client._read_string(conn)
                    # prefijo "c>" para mensajes recibidos con adjunto
                    # flush=True asegura que se vea en Docker inmediatamente
                    print(f"\nc> MESSAGE {id_msg} FROM {remitente}\n   {texto}\n   END\n   FILE {fichero}", flush=True)
                    print("c> ", end="", flush=True)

                elif op == "SEND_MESS_ATTACH_ACK":
                    # En caso de "SEND_MESS_ATTACH_ACK" recepción de id_msg y fichero para posterior impresión
                    id_msg = client._read_string(conn)
                    fichero = client._read_string(conn)
                    # flush=True asegura que se vea en Docker inmediatamente
                    print(f"\nc> SENDATTACH MESSAGE {id_msg} {fichero} OK")
                    print("c> ", end="", flush=True)
                elif op == "GET FILE":
                    # Leemos quién nos pide y qué nos pide
                    user_requesting = client._read_string(conn)
                    filename_requested = client._read_string(conn)
                    
                    try:
                        # Enviamos el contenido binario del fichero solicitado
                        with open(filename_requested, "rb") as f:
                            conn.sendall(f.read())
                    except (FileNotFoundError, PermissionError, OSError):
                        # FileNotFoundError: el fichero pedido no existe en este cliente
                        # PermissionError: no tenemos permisos de lectura sobre el fichero
                        # OSError: error de I/O al leer el fichero o al enviarlo por el socket
                        pass
                #Cerramos la conexión temporal
                conn.close()
            except (OSError, EOFError):
                # OSError: el socket de escucha fue cerrado por DISCONNECT o QUIT
                # EOFError: fin de stream inesperado al leer del socket
                break

    # *
    # * @param user - User name to register in the system
    # * 
    # * @return OK if successful
    # * @return USER_ERROR if the user is already registered
    # * @return ERROR if another error occurred
    @staticmethod
    def  register(user) :
        """
        Protocolo (8.1):
          1. Conectar al servidor.
          2. Enviar "REGISTER\0".
          3. Enviar nombre de usuario con '\0'.
          4. Recibir 1 byte: 0=OK, 1=ya existe, 2=error.
          5. Cerrar conexión.
        """
        # Futuro socket
        s = None
        try:
            # 1. Conectar al servidor
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))

            # 2. Enviar la operación
            s.sendall("REGISTER\0".encode('utf-8'))

            # 3. Enviar el nombre de usuario
            s.sendall((user + "\0").encode('utf-8'))

            # 4. Recibir resultado (1 byte)
            res = s.recv(1)
            # Gestión por si hay fallo
            if not res:
                print("c> REGISTER FAIL")
                return client.RC.ERROR

            #Tranformación del código recibido a entero para posterior evluación
            res_code = int.from_bytes(res, byteorder='little')

            if res_code == 0:
                # Caso correcto
                print("c> REGISTER OK")
                return client.RC.OK
            elif res_code == 1:
                # El servidor indica que el nombre ya está en uso
                print("c> USERNAME IN USE")
                return client.RC.USER_ERROR
            else:
                # Código 2 u otro: error genérico
                print("c> REGISTER FAIL")
                return client.RC.ERROR

        except (ConnectionRefusedError, TimeoutError, OSError):
            # ConnectionRefusedError: el servidor no está arrancado o rechaza la conexión
            # TimeoutError: el servidor no responde en tiempo
            # OSError: error genérico de red o socket
            # Error de red o servidor caído
            print("c> REGISTER FAIL")
            return client.RC.ERROR

        finally:
            # 5. Cerrar la conexión siempre
            if s:
                s.close()

    # *
    # 	 * @param user - User name to unregister from the system
    # 	 * 
    # 	 * @return OK if successful
    # 	 * @return USER_ERROR if the user does not exist
    # 	 * @return ERROR if another error occurred
    @staticmethod
    def  unregister(user) :
        """
        Protocolo (sección 8.2):
          1. Conectar al servidor.
          2. Enviar "UNREGISTER\0".
          3. Enviar nombre de usuario con '\0'.
          4. Recibir 1 byte: 0=OK, 1=no existe, 2=error.
          5. Cerrar conexión.
        """
        # Futuro socket
        s = None
        try:
            # 1. Conectar al servidor
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))

            # 2. Enviar la operación
            s.sendall("UNREGISTER\0".encode('utf-8'))

            # 3. Enviar el nombre de usuario
            s.sendall((user + "\0").encode('utf-8'))

            # 4. Recibir resultado (1 byte)
            res = s.recv(1)
            # Gestión de posibles errores
            if not res:
                print("c> UNREGISTER FAIL")
                return client.RC.ERROR

            # Paso de código recibido a entero para posterior evaluación
            res_code = int.from_bytes(res, byteorder='little')

            if res_code == 0:
                # Caso correcto
                print("c> UNREGISTER OK")
                return client.RC.OK
            elif res_code == 1:
                # El usuario indicado no existe en el servidor
                print("c> USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            else:
                # Código 2 u otro: error genérico
                print("c> UNREGISTER FAIL")
                return client.RC.ERROR

        except (ConnectionRefusedError, TimeoutError, OSError):
            # ConnectionRefusedError: el servidor no está arrancado o rechaza la conexión
            # TimeoutError: el servidor no responde en tiempo
            # OSError: error genérico de red o socket
            print("c> UNREGISTER FAIL")
            return client.RC.ERROR

        finally:
            # 5. Cerrar la conexión siempre
            if s:
                s.close()


    # *
    # * @param user - User name to connect to the system
    # * 
    # * @return OK if successful
    # * @return USER_ERROR if the user does not exist or if it is already connected
    # * @return ERROR if another error occurred
    @staticmethod
    def  connect(user) :
        """
        Protocolo (sección 8.3):
          1. Buscar un puerto libre y crear el socket de escucha.
          2. Poner el socket en listen() ANTES de arrancar el hilo (evita race condition).
          3. Crear el hilo receptor (daemon).
          4. Conectar al servidor y enviar: "CONNECT\0" + nombre + puerto.
          5. Recibir 1 byte: 0=OK, 1=no existe, 2=ya conectado, 3=error.
          6. Si OK, guardar el nombre en _username.
          7. Si fallo, cerrar el socket de escucha y limpiar _listener_sock.
          8. Cerrar la conexión de control.
        """
        # Barrera local: solo un usuario conectado por terminal a la vez
        if client._username != "":
            print("c> CONNECT FAIL")
            return client.RC.ERROR

        # futuro socket y socket de hilo secundario
        s           = None
        listen_sock = None
        try:
            # 1. Buscar puerto libre (bind al puerto 0 → el SO asigna uno disponible)
            listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            listen_sock.bind(('0.0.0.0', 0))
            my_port = listen_sock.getsockname()[1]

            # 2. Poner en escucha antes de arrancar el hilo
            listen_sock.listen(5)

            # 3. Crear y arrancar el hilo receptor 
            t = threading.Thread(target=client._listener_thread, args=(listen_sock,))
            t.daemon = True
            t.start()

            # Guardamos el socket para poder cerrarlo en DISCONNECT
            client._listener_sock = listen_sock

            # 4. Enviar solicitud de conexión al servidor
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))

            # Envío de codigo e información necesaria
            s.sendall("CONNECT\0".encode('utf-8'))
            s.sendall((user + "\0").encode('utf-8'))
            # El puerto se envía como cadena de texto, p.ej. "54321\0"
            s.sendall((str(my_port) + "\0").encode('utf-8'))

            # 5. Recibir resultado (1 byte)
            res = s.recv(1)
            # Gestión de posibles errores
            if not res:
                print("c> CONNECT FAIL")
                listen_sock.close()
                client._listener_sock = None
                return client.RC.ERROR

            # Transformación de código de operación para posterior evaluación
            res_code = int.from_bytes(res, byteorder='little')

            if res_code == 0:
                # 6. Éxito: guardar el nombre del usuario conectado
                print("c> CONNECT OK")
                client._username = user
                return client.RC.OK
            elif res_code == 1:
                # Caso en el que usuario no existe
                print("c> CONNECT FAIL, USER DOES NOT EXIST")
                # 7. Limpiar el socket de escucha en caso de fallo
                listen_sock.close()
                client._listener_sock = None
                return client.RC.USER_ERROR
            elif res_code == 2:
                # Caso en el que el usuario ya está conectado
                print("c> USER ALREADY CONNECTED")
                # Limpia el socket de escucha
                listen_sock.close()
                client._listener_sock = None
                return client.RC.USER_ERROR
            else:
                # Código 3 u otro error genérico
                print("c> CONNECT FAIL")
                listen_sock.close()
                client._listener_sock = None
                return client.RC.ERROR

        except (ConnectionRefusedError, TimeoutError, OSError):
            # ConnectionRefusedError: el servidor no está arrancado o rechaza la conexión
            # TimeoutError: el servidor no responde en tiempo
            # OSError: error genérico de red o socket
            print("c> CONNECT FAIL")
            if listen_sock:
                listen_sock.close()
                client._listener_sock = None
            return client.RC.ERROR

        finally:
            # 8. Cerrar la conexión de control siempre
            if s:
                s.close()

    # *
    # * 
    # * @return OK if successful
    # * @return USER_ERROR if the user does not exist or if it is already connected
    # * @return ERROR if another error occurred
    @staticmethod
    def  users() :
        """
        Protocolo (sección 8.7):
          1. Conectar al servidor.
          2. Enviar "USERS\0" + nombre del usuario conectado.
          3. Recibir 1 byte: 0=OK, 1=no conectado, 2=error.
          4. Si OK: recibir número de conectados y luego un nombre por cadena.
          5. Cerrar conexión.

        Salida (sección 6.8):
          c> CONNECTED USERS (N users connected) OK
             user1
             user2
        """
        # Usamos el nombre guardado en _username (fijado durante CONNECT)
        user = client._username

        # Si no hay nombre, es que no se ha hecho CONNECT
        if not user:
            print("c> CONNECTED USERS FAIL, USER IS NOT CONNECTED")
            return client.RC.ERROR

        # Socket futuro
        s = None
        try:
            # 1. Conectar al servidor
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))

            # 2. Enviar operación y nombre de usuario
            s.sendall("USERS\0".encode('utf-8'))
            s.sendall((user + "\0").encode('utf-8'))

            # 3. Recibir resultado (1 byte)
            res = s.recv(1)
            # Gestión de posibles errores
            if not res:
                print("c> CONNECTED USERS FAIL")
                return client.RC.ERROR

            # Transformación de código a entero para su posterior evaluación
            res_code = int.from_bytes(res, byteorder='little')

            if res_code == 0:
                # Caso correcto
                # 4. Leer número de usuarios conectados y luego cada nombre
                num_users_str = client._read_string(s)
                num_users     = int(num_users_str)
                
                # Formato de cabecera exigido
                print(f"c> CONNECTED USERS ({num_users} users connected) OK")
                
                for _ in range(num_users):
                    # Recibe la cadena completa: "Juan :: 127.0.0.1 :: 54321"
                    data = client._read_string(s) 
                    
                    # 1. Parseamos la cadena primero para separar el nombre de la red
                    parts = data.split(" :: ")
                    
                    if len(parts) >= 3:
                        # 2. Imprimimos solo el nombre (lo que ve el usuario final)
                        print(f"   {parts[0]}")
                        
                        # 3. Guardamos IP y puerto internamente para que GETFILE funcione
                        client._user_table[parts[0]] = (parts[1], int(parts[2]))
                    else:
                        # Caso de seguridad por si el formato de red fallara
                        print(f"   {data}")

                return client.RC.OK

            elif res_code == 1:
                # El usuario solicitante no está conectado en el servidor
                print("c> CONNECTED USERS FAIL, USER IS NOT CONNECTED")
                return client.RC.USER_ERROR
            else:
                # Código 2 u otro: error genérico
                print("c> CONNECTED USERS FAIL")
                return client.RC.ERROR

        except (ConnectionRefusedError, TimeoutError, OSError, ValueError):
            # ConnectionRefusedError: el servidor no está arrancado o rechaza la conexión
            # TimeoutError: el servidor no responde en tiempo
            # OSError: error genérico de red o socket
            # ValueError: num_users_str no es convertible a entero
            print("c> CONNECTED USERS FAIL")
            return client.RC.ERROR

        finally:
            # 5. Cerrar la conexión siempre
            if s:
                s.close()



    # *
    # * @param user - User name to disconnect from the system
    # * 
    # * @return OK if successful
    # * @return USER_ERROR if the user does not exist
    # * @return ERROR if another error occurred
    @staticmethod
    def  disconnect(user) :
        """
        Protocolo (sección 8.4):
          1. Conectar al servidor.
          2. Enviar "DISCONNECT\0" + nombre de usuario.
          3. Recibir 1 byte: 0=OK, 1=no existe, 2=no conectado, 3=error.
          4. En CUALQUIER caso parar el hilo de escucha y limpiar _username.
             (El enunciado sección 6.5 lo exige expresamente.)
          5. Cerrar la conexión de control.
        """
        # Futuro socket
        s = None
        try:
            # 1. Conectar al servidor
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))

            # 2. Enviar operación y nombre de usuario
            s.sendall("DISCONNECT\0".encode('utf-8'))
            s.sendall((user + "\0").encode('utf-8'))

            # 3. Recibir resultado (1 byte)
            res = s.recv(1)
            # Gestión de posibles errores
            if not res:
                print("c> DISCONNECT FAIL")
                return client.RC.ERROR

            # Transformación del código a entero para posterior evaluación
            res_code = int.from_bytes(res, byteorder='little')

            if res_code == 0:
                # Caso correcto
                print("c> DISCONNECT OK")
                return client.RC.OK
            elif res_code == 1:
                # Caso usuario no existe
                print("c> DISCONNECT FAIL, USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            elif res_code == 2:
                # Caso usuario no está conectado
                print("c> DISCONNECT FAIL, USER NOT CONNECTED")
                return client.RC.USER_ERROR
            else:
                # Código 3 u otro error genérico
                print("c> DISCONNECT FAIL")
                return client.RC.ERROR

        except (ConnectionRefusedError, TimeoutError, OSError):
            # ConnectionRefusedError: el servidor no está arrancado o rechaza la conexión
            # TimeoutError: el servidor no responde en tiempo
            # OSError: error genérico de red o socket
            print("c> DISCONNECT FAIL")
            return client.RC.ERROR

        finally:
            # 5. Cerrar la conexión de control
            if s:
                s.close()

            # 6. siempre paramos el hilo de escucha y reseteamos el estado.
            if client._listener_sock:
                try:
                    client._listener_sock.close()
                except OSError:
                    # OSError: el socket ya estaba cerrado previamente, ignoramos
                    pass
                client._listener_sock = None
            client._username = ""

    # *
    # * @param user    - Receiver user name
    # * @param message - Message to be sent
    # * # * @return OK if the server had successfully delivered the message
    # * @return USER_ERROR if the user is not connected (the message is queued for delivery)
    # * @return ERROR the user does not exist or another error occurred
    @staticmethod
    def  send(user,  message) :
        """
        Protocolo (sección 8.5):
          1. Conectar al servidor.
          2. Enviar "SEND\0" + remitente + destinatario + mensaje.
          3. Recibir 1 byte: 0=OK (+ id), 1=no existe, 2=error.
          4. Cerrar la conexión.

        El ACK de entrega ("c> SEND MESSAGE <id> OK") llega de forma
        asíncrona a través del hilo de escucha (sección 8.6).
        """
        # El servidor usa _username como remitente, sin CONNECT no sabemos quién somos
        if not client._username:
            print("c> SEND FAIL")
            return client.RC.ERROR

        # Futuro socket
        s = None
        try:
            # 1. Llamada a la función auxiliar para normalizar.
            message = client._normalize_message(message)
            # El protocolo limita el mensaje a 256 bytes incluyendo '\0'
            if len(message) > 255:
                message = message[:255]

            # 1. Conectar al servidor
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))

            # 2. Enviar los campos del protocolo
            s.sendall("SEND\0".encode('utf-8'))
            # El remitente somos nosotros (guardado en _username durante CONNECT)
            s.sendall((client._username + "\0").encode('utf-8'))
            s.sendall((user + "\0").encode('utf-8'))
            s.sendall((message + "\0").encode('utf-8'))

            # 3. Recibir resultado (1 byte)
            res = s.recv(1)
            # Gestión de posibles errores
            if not res:
                print("c> SEND FAIL")
                return client.RC.ERROR

            # Transformación de código a entero para posterior evaluación
            res_code = int.from_bytes(res, byteorder='little')

            if res_code == 0:
                # En caso de éxito, el servidor envía a continuación el id del mensaje
                msg_id = client._read_string(s)
                print(f"c> SEND OK - MESSAGE {msg_id}")
                return client.RC.OK
            elif res_code == 1:
                # El destinatario no existe en el servidor
                print("c> SEND FAIL, USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            else:
                # Código 2 u otro error genérico
                print("c> SEND FAIL")
                return client.RC.ERROR

        except (ConnectionRefusedError, TimeoutError, OSError):
            # ConnectionRefusedError: el servidor no está arrancado o rechaza la conexión
            # TimeoutError: el servidor no responde en tiempo
            # OSError: error genérico de red o socket
            print("c> SEND FAIL")
            return client.RC.ERROR

        finally:
            # 4. Cerrar la conexión siempre
            if s:
                s.close()

    # *
    # * @param user    - Receiver user name
    # * @param file    - file  to be sent
    # * @param message - Message to be sent
    # * # * @return OK if the server had successfully delivered the message
    # * @return USER_ERROR if the user is not connected (the message is queued for delivery)
    # * @return ERROR the user does not exist or another error occurred
    @staticmethod
    def sendAttach(user, message, filename):
        """
        Protocolo Sección 2.2: SENDATTACH + remitente + destinatario + mensaje + fichero
        """
        # El servidor usa _username como remitente, sin CONNECT no sabemos quién somos
        if not client._username:
            print("c> SENDATTACH FAIL, USER NOT CONNECTED")
            return client.RC.ERROR

        # Futuro socket
        s = None
        try:
            # 1. Normalización de mensaje
            message = client._normalize_message(message)

             # Conectar al servidor
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))

            # Envío de los 5 campos
            s.sendall("SENDATTACH\0".encode('utf-8'))
            s.sendall((client._username + "\0").encode('utf-8'))
            s.sendall((user + "\0").encode('utf-8'))
            s.sendall((message + "\0").encode('utf-8'))
            s.sendall((filename + "\0").encode('utf-8'))

            # Recepción código
            res = s.recv(1)
            # Gestión de posibles errores
            if not res: return client.RC.ERROR
            
            # Transformación de código a entero para posterior evaluación
            res_code = int.from_bytes(res, byteorder='little')

            if res_code == 0:
                # Mensaje enviado correctamente
                msg_id = client._read_string(s)
                print(f"c> SENDATTACH OK - MESSAGE {msg_id}")
                return client.RC.OK
            elif res_code == 1:
                # Error, el remitente no existe
                print("c> SENDATTACH FAIL, USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            else:
                # Cualquier otro error
                print("c> SENDATTACH FAIL")
                return client.RC.ERROR
        except (ConnectionRefusedError, TimeoutError, OSError):
            # ConnectionRefusedError: el servidor no está arrancado o rechaza la conexión
            # TimeoutError: el servidor no responde en tiempo
            # OSError: error genérico de red o socket
            print("c> SENDATTACH FAIL")
            return client.RC.ERROR
        finally:
            if s: s.close()
    @staticmethod
    def get_file(user, remote_file, local_file):
        # 1. Verificar si tenemos los datos del usuario. Si no, refrescar.
        if user not in client._user_table:
            # Llamada a users() para actualizar _user_table
            client.users()
            
        # 2. Si tras el refresco sigue sin estar, el usuario está desconectado
        if user not in client._user_table:
            print("c> FILE TRANSFER FAILED, user not connected.")
            return client.RC.USER_ERROR

        # Obtenemos la IP y el puerto del usuario recibido
        ip_peer, port_peer = client._user_table[user]
        
        s_p2p = None
        try:
            # 3. Conexión directa al hilo de escucha del otro cliente
            s_p2p = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s_p2p.connect((ip_peer, port_peer))

            # 4. Protocolo P2P 
            s_p2p.sendall("GET FILE\0".encode('utf-8'))
            s_p2p.sendall((client._username + "\0").encode('utf-8'))
            s_p2p.sendall((remote_file + "\0").encode('utf-8'))

            # 5. Recepción del contenido y guardado local
            # Usamos "wb" para asegurar que la transferencia sea binaria
            with open(local_file, "wb") as f:
                while True:
                    data = s_p2p.recv(1024)
                    if not data:
                        break
                    f.write(data)
            
            print(f"c> FILE {remote_file} TRANSFERRED OK")
            return client.RC.OK

        except (ConnectionRefusedError, TimeoutError, OSError, IOError):
            # ConnectionRefusedError: el cliente remoto no está disponible
            # TimeoutError: el cliente remoto no responde en tiempo
            # OSError / IOError: error de red o al escribir el fichero local
            print("c> FILE TRANSFER FAILED")
            return client.RC.ERROR
        finally:
            if s_p2p: s_p2p.close()
    # *
    # **
    # * @brief Command interpreter for the client. It calls the protocol functions.
    @staticmethod
    def shell():
        while (True) :
            try :
                command = input("c> ")
                if not command.strip(): continue # Ignora si solo pulsas Enter

                # split() sin parámetros limpia todos los espacios sobrantes
                line = command.split(" ")
                
                if (len(line) > 0 and line[0]!=""):
                    line[0] = line[0].upper()

                    if (line[0]=="REGISTER") :
                        if (len(line) == 2) :
                            client.register(line[1])
                        else :
                            print("Syntax error. Usage: REGISTER <userName>")

                    elif(line[0]=="UNREGISTER") :
                        if (len(line) == 2) :
                            client.unregister(line[1])
                        else :
                            print("Syntax error. Usage: UNREGISTER <userName>")

                    elif(line[0]=="CONNECT") :
                        if (len(line) == 2) :
                            client.connect(line[1])
                        else :
                            print("Syntax error. Usage: CONNECT <userName>")

                    elif(line[0]=="DISCONNECT") :
                        if (len(line) == 2) :
                            client.disconnect(line[1])
                        else :
                            print("Syntax error. Usage: DISCONNECT <userName>")

                    elif(line[0]=="USERS") :
                        if (len(line) == 1) :
                            client.users()
                        else :
                            print("Syntax error. Usage: CONNECTED_USERS <userName>")

                    elif(line[0]=="SEND") :
                        if (len(line) >= 3) :
                            #  Borramos dos primeras palabras
                            message = ' '.join(line[2:])
                            client.send(line[1], message)
                        else :
                            print("Syntax error. Usage: SEND <userName> <message>")

                    elif(line[0]=="SENDATTACH") :
                        if (len(line) >= 4) :
                            # El último elemento es el nombre del archivo
                            filename = line[-1]
                            # Todo lo que hay entre el usuario y el archivo es el mensaje
                            message = ' '.join(line[2:-1])
                            client.sendAttach(line[1], message, filename)
                        else :
                            print("Syntax error. Usage: SENDATTACH <userName> <message> <fileName>")
                    elif(line[0]=="GETFILE") :
                        if (len(line) == 4) :
                            # Sintaxis: GETFILE <userName> <remoteFileName> <localFileName>
                            client.get_file(line[1], line[2], line[3])
                        else :
                            print("Syntax error. Usage: GETFILE <userName> <remoteFile> <localFile>")
                    elif(line[0]=="QUIT") :
                        if (len(line) == 1) :
                            break
                        else :
                            print("Syntax error. Use: QUIT")
                    else :
                        print("Error: command " + line[0] + " not valid.")
            except Exception as e:
                print("Exception: " + str(e))

    # *
    # * @brief Prints program usage
    @staticmethod
    def usage() :
        print("Usage: python3 client.py -s <server> -p <port>")


    # *
    # * @brief Parses program execution arguments
    @staticmethod
    def  parseArguments(argv) :
        parser = argparse.ArgumentParser()
        parser.add_argument('-s', type=str, required=True, help='Server IP')
        parser.add_argument('-p', type=int, required=True, help='Server Port')
        args = parser.parse_args()

        if (args.s is None):
            parser.error("Usage: python3 client.py -s <server> -p <port>")
            return False

        if ((args.p < 1024) or (args.p > 65535)):
            parser.error("Error: Port must be in the range 1024 <= port <= 65535");
            return False;
        
        client._server = args.s
        client._port = args.p

        return True


    # ******************** MAIN *********************
    @staticmethod
    def main(argv) :
        if (not client.parseArguments(argv)) :
            client.usage()
            return

        #  Write code here
        client.shell()
        print("+++ FINISHED +++")
    

if __name__=="__main__":
    client.main([])