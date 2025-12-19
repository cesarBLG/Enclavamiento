# Parámetros generales
- La dirección del broker deberá ser configurable. Deben aceptarse direcciones IP, nombres DNS y mDNS.
- No existirá autenticación

# Gestión de desconexión
Todos los clientes tendrán configurado un mensaje de last will, publicado en el topic "desconexion"
con contenido el id del cliente.

Al establecerse la conexión con el broker, se enviará al topic "desconexion/*id_cliente*" un mensaje
con retain, cuyo contenido es una lista separada por saltos de línea, que indica todos los topics
que proporciona el cliente.

Esto permite asumir el estado seguro para todos los topics cuya información se obtiene
a través de MQTT. Existe un gestor de conexión que monitoriza el topic "desconexion", y ante cualquier
desconexión, publica el mensaje "\"desconexion\"" a todos los topics proporcionados
por el cliente que se ha desconectado. Este gestor de conexión publica el mensaje "on" (sin comillas)
al topic "gestor_conexion" al conectarse, y el mensaje "off" al desconectarse (last will).
El mensaje "off" es de tipo retained.

El cliente asumirá el estado seguro para un topic si:
- Recibe el mensaje "\"desconexion\"" (con comillas) para ese topic
- Recibe el mensaje de "off" en el topic gestor_conexion
- El cliente pierde la conexión

Además, el cliente rechazará nuevos mensajes si recibe el mensaje "off" del gestor de conexión,
hasta que vuelva a estar disponible.

# Topic reference
Todos los topics relativos a elementos de campo siguen el formato *tipo*/*dependencia*/*id*/*topic*
Si el ID contiene barras (/), se cambiarán por guiones bajos (_)
- cejes/+/+/event: JSON string, evento de contador de ejes. En caso de fallo de detección, el mensaje
  es "Error". En caso de detección correcta en sentido nominal o inverso, el mensaje será
  "Nominal[\:*num*]" o "Reverse[\":*num*]", respectivamente. Opcionalmente, se puede señalizar más de
  una detección consecutiva en el mismo sentido añadiendo el número al mensaje, separado del sentido
  por dos puntos (:).
- cv/+/+/state: JSON object, estado de ocupación del CV
- signal/+/+/state: JSON object, aspecto de la señal
- bloqueo/*estacion1*/*estacion2*\[*via*\]/state: JSON object, estado del bloqueo en *estacion1*
  hacia *estacion2*
- bloqueo/*estacion1*/*estacion2*\[*via*\]/colateral: JSON object, transmisión a la *estacion1*
  del estado de *estacion2* a efectos del bloqueo (comunicación entre enclavamientos para gestionar
  el bloqueo)
- pn/+/+/cierre: bool, indica si el enclavamiento ordena el cierre
- pn/+/+/protegido: bool, indica si el PN comprueba en cerrado
