import logging
from wsgiref.simple_server import make_server
from spyne import Application, ServiceBase, Unicode, rpc
from spyne.protocol.soap import Soap11
from spyne.server.wsgi import WsgiApplication

# Definición del servicio basada en la clase ServiceBase
class NormalizadorService(ServiceBase):
    # Definimos el método remoto con el decorador @rpc 
    # Recibe un Unicode y devuelve un Unicode
    @rpc(Unicode, _returns=Unicode)
    def normalizar(ctx, texto):
        if texto is None:
            return ""
        # Lógica de normalización: elimina espacios al principio/final y dobles espacios
        return " ".join(texto.split())

# Configuración de la aplicación SOAP 1.1 
application = Application(
    services=[NormalizadorService],
    tns='http://servicio.web.normalizador/',
    in_protocol=Soap11(validator='lxml'),
    out_protocol=Soap11()
)

# Envoltura para el servidor WSGI 
wsgi_app = WsgiApplication(application)

if __name__ == '__main__':
    # Configuración de logs 
    logging.basicConfig(level=logging.INFO)
    logging.info("Escuchando en http://127.0.0.1:8000")
    logging.info("WSDL disponible en: http://127.0.0.1:8000/?wsdl")
    
    # Arrancamos el servidor en el puerto 8000
    server = make_server('127.0.0.1', 8000, wsgi_app)
    server.serve_forever()