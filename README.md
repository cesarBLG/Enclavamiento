# Enclavamiento
Enclavamiento electrónico ferroviario configurable.

# Características
- Desarrollado en C++
- Ejecutable estático. Todas las funciones del enclavamiento, circuitos de vía, aparatos y señales se describen y configuran por medio de un archivo JSON.
- Circuitos de vía basados en contadores de ejes.
- Circuitos de vía lineales, agujas y cruzamientos.
- Movimientos de itinerario, rebase autorizado y maniobra centralizada.
- Formación y disolución automática de itinerarios.
- Bloqueos automáticos según NAS 818.
- Pasos a nivel enclavados y afectados.
- Múltiples modalidades de servicio intermitente.
- Secuencia de aspecto de señales configurable.
- Gestión de mando local/centralizado.
- Remota de comunicación con CTC (parcialmente compatible con NAS 831).

# Diferencias con enclavamientos de ADIF
- Apertura de señal con CV pendiente de normalización.
- Circulación en régimen de marcha a la vista al amparo del bloqueo con cantón ocupado.
- Normalización automática de CVs si se libera un porcentaje de los ejes totales en el cantón.
- Disolución automática de itinerarios y gestión de la prioridad de FAI entre colaterales.

# Compilación
Clonar el repositorio:

```bash
git clone https://github.com/cesarBLG/Enclavamiento.git
cd VisorCTC
```

Compilar (requiere la librería mosquitto):
```bash
mkdir build
cd build
cmake ..
cmake --build .
```

# Ejecución
- Activar un broker MQTT sin autenticación
- Activar el gestor de conexiones de nodos:
```bash
./gestor_conexion <ip_broker>
```
- Iniciar los enclavamientos
```
./enclavamiento <config_ENCE.json>
```

# Configuración y uso
Consultar los documentos en la carpeta `doc/`
