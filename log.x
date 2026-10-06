/* log.x - Definición del servicio de log RPC */

program LOGPROG {
    version LOGVERS {
        /* Procedimiento que recibe una cadena y devuelve un entero */
        int PRINT_LOG(string) = 1;
    } = 1;
} = 99;