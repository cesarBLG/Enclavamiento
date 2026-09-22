# Referencia de configuración JSON

Este documento describe el JSON de configuración del enclavamiento. Salvo que se
indique lo contrario, una referencia a un elemento puede escribirse como
`"Dependencia:Id"` o `"Id"`; las referencias cortas se resuelven en la dependencia que
las contiene. Los identificadores que son claves de un objeto son siempre el
identificador corto del elemento.

`Lado` admite `"Par"` o `"Impar"`. Las posiciones de aparatos y los pares de
pines se representan como `[pin salida par, pin salida impar]`; los pines se numeran desde cero.
Los tiempos de los parámetros y rutas se expresan en segundos.

## Estructura superior

```json
{
  "MQTT": { "Name": "ence-1", "Host": "127.0.0.1" },
  "ParámetrosPredeterminados": {},
  "Dependencias": { "EST": {} }
}
```

### `MQTT`

| Campo | Tipo | Obligatorio | Descripción |
| --- | --- | --- | --- |
| `Name` | cadena | Sí | Identificador del cliente MQTT y contenido del mensaje de desconexión. |
| `Host` | cadena | Sí | Host del broker MQTT. El puerto está fijado en el código a `1883`. |

### `ParámetrosPredeterminados`

El objeto superior es obligatorio, aunque todos sus campos tienen valor por
defecto.

| Campo | Tipo; valor por defecto | Descripción |
| --- | --- | --- |
| `DiferímetroDAI1` | número; `30` | Temporizador de disolución artificial de itinerario para zona 1. |
| `DiferímetroDAI2` | número; `150` | Temporizador DAI zona 2. |
| `DiferímetroDEI` | número; `180` | Temporizador de disolución de emergencia. |
| `PrenormalizaciónCV` | número; `180` | Tiempo de prenormalización automática de un CV de contadores de ejes. |
| `PrenormalizaciónCVTren` | número; `20` | Tiempo de prenormalización cuando se ha liberado un porcentaje de los ejes de un tren. |
| `EspaciadoFAI` | número; `20` | Espera entre solicitudes de formación automática de itinerario (FAI). |
| `FracciónEjesPrenormalización` | número; `0.5` | Fracción de ejes de un tren liberados que permite la prenormalización. |
| `DeslizamientoBloqueo` | booleano; `false` | Habilita el comportamiento de deslizamiento de entrada del bloqueo (avanzada protege maniobras). |
| `AspectoDesviada` | aspecto; `"AnuncioParada"` | Aspecto por defecto para itinerarios por desviada. |
| `AnuncioPrecaución` | booleano; `true` | Habilita el anuncio de precaución de la señal anterior con rutas a desviada. |

Los aspectos admitidos son `Parada`, `RebaseAutorizado`,
`RebaseAutorizadoDestellos`, `MovimientoAutorizado`, `ParadaSelectiva`,
`ParadaSelectivaDestellos`, `ParadaDiferida`, `Precaución`, `AnuncioParada`,
`AnuncioPrecaución` y `VíaLibre`.

## Dependencias

`Dependencias` es un objeto cuyas claves identifican estaciones o dependencias.
Solo se crean dependencias controladas: `Controlada: false` permite declarar
elementos de una estación colateral requeridos por el enclavamiento, pero no crea su
lógica de mando.

| Campo | Tipo | Por defecto | Descripción |
| --- | --- | --- | --- |
| `Controlada` | booleano | `true` | Crea y controla la dependencia local. |
| `CVs` | objeto | — | Circuitos de vía, indexados por identificador. |
| `Secciones` | objeto | — | Secciones de vía, indexadas por identificador. |
| `PNs` | objeto | — | Pasos a nivel, indexados por identificador. |
| `Señales` | objeto | — | Señales, indexadas por identificador. |
| `Bloqueos` | array | — | Bloqueos de la dependencia. |
| `DestinosRuta` | objeto | — | Destinos de rutas, indexados por identificador. |
| `Rutas` | array | — | Rutas definidas para la dependencia. |
| `ServicioIntermitente` | objeto | — | Configuración de estación cerrada/servicio intermitente. |

### CV (`CVs`)

Cada entrada es un objeto. Si contiene `ContadoresEjes` se implementa como CV
de contadores de ejes; de lo contrario se considera CV tradicional y se recibe su estado por MQTT.

| Campo | Tipo | Por defecto | Descripción |
| --- | --- | --- | --- |
| `Tipo` | `"Lineal"`, `"Aguja"` o `"Cruzamiento"` | `"Aguja"` (`"Cruzamiento"`) si el id comienza por `CVA`(`CVX`); si no, `"Lineal"` | Tipo de circuito de vía según las secciones que lo formen. |
| `ContadoresEjes` | objeto | — | Activa el CV de contadores de ejes. Sus claves son ids de contadores físicos y sus valores son objetos de posición. |

Objeto de cada contador de `ContadoresEjes`:

| Campo | Tipo | Por defecto | Descripción |
| --- | --- | --- | --- |
| `Lado` | `Lado` | — | **Obligatorio.** Lado por el que se ubica el contador. |
| `Reverse` | booleano | `false` | Invierte la interpretación del sentido del evento. |
| `Liberar` | booleano | `true` | El contador participa en la liberación de ejes. |
| `Ocupar` | booleano | `true` | El contador participa en la ocupación; también se considera desconectado hasta recibir datos. |

### Sección (`Secciones`)

Las secciones de vía definen la topología de la estación.
Incluyen secciones lineales, cruzamientos y agujas. Cada CV puede incluir una o varias secciones.
En caso de CV lineales, debe haber una única sección por CV, con el mismo nombre que el CV:

| Campo | Tipo | Por defecto | Descripción |
| --- | --- | --- | --- |
| `Tipo` | `"Lineal"`, `"Cruzamiento"` o `"Aguja"` | `"Lineal"` | Selecciona la clase de sección. |
| `CV` | referencia a CV | mismo id que la sección | CV al que pertenece la sección. Si no existe, la sección queda sin CV asociado. |
| `Bloqueo` | referencia a bloqueo | — | Bloqueo al que pertenece la sección. |
| `Trayecto` | booleano | `true` si pertenece a bloqueo | Indica que la sección corresponde a trayecto (fuera de la estación). |
| `Conexiones` | objeto de lados | — | Conexión con otras secciones. En cada lado, un array de conexiones, en el orden de pin de salida. Es la forma habitual para lineales y cruzamientos. |

Un objeto de lados tiene forma `{"Impar": valor, "Par": valor}`. Una
conexión puede ser una cadena con el id del destino, o un objeto:

| Campo de conexión | Tipo | Por defecto | Descripción |
| --- | --- | --- | --- |
| `Id` | referencia a sección | — | **Obligatorio.** Sección conectada. |
| `InvertirParidad` | booleano | `false` | Cambia el sentido de circulación al atravesar la conexión. |

#### Aguja

Además de los campos comunes, una aguja requiere `Lado`.

| Campo | Tipo | Por defecto | Descripción |
| --- | --- | --- | --- |
| `Lado` | `Lado` | — | **Obligatorio.** Sentido en el que se toma la aguja de punta. |
| `SecciónPunta` | conexión | — | Conexión de la punta. |
| `SeccionesTalón` | array de conexiones | — | Conexiones del talón, normalmente normal e invertida en posiciones 0 y 1. |
| `Talonable` | booleano | `true` | Permite el talonamiento de la aguja. |
| `PosiciónMuelle` | número | — | Configura aguja talonable con muelle: `0` normal, `1` invertida. |
| `Escape` | referencia a aguja | — | Aguja con la que forma escape. |

#### Cruzamiento

No tiene campos propios. Debe proporcionar las dos conexiones de cada lado para
mantener los dos itinerarios independientes (del pin 0 al 0 y del 1 al 1)

### Paso a nivel (`PNs`)

| Campo | Tipo | Por defecto | Descripción |
| --- | --- | --- | --- |
| `Sección` | referencia a sección | — | **Obligatorio.** Sección que protege el PN. |
| `TiempoApertura` | objeto de lados con números | `{"Impar":0,"Par":0}` | Temporizador de apertura automática por lado tras una ocupación, en segundos. `0` indica ausencia de temporizador. |
| `Tipo` | objeto de lados | automático en trayecto; enclavado en otro caso | Comportamiento por lado: `"Automático"`, `"Afectado"` o `"Enclavado"`. |

### Señal (`Señales`)

| Campo | Tipo | Por defecto | Descripción |
| --- | --- | --- | --- |
| `Lado` | `Lado` | — | **Obligatorio.** Sentido de circulación protegido. |
| `Tipo` | tipo de señal | — | **Obligatorio.** `Entrada`, `Salida`, `Avanzada`, `Maniobra`, `Retroceso`, `Intermedia` o `PostePuntoProtegido`. |
| `Sección` | referencia a sección | — | **Obligatorio.** Sección inmediatamente posterior a la señal. |
| `Pin` | entero | `0` | Pin de entrada a la sección protegida por la señal. |
| `Bloqueo` | referencia a bloqueo | — | Bloqueo asociado para señales de salida o de trayecto. |
| `AspectoAnteriorSeñal` | objeto `aspecto → aspecto` | `{ "ParadaDiferida": "VíaLibre" }` | Límite de aspecto que esta señal impone a la señal anterior. |
| `AspectoCanton` | objeto `EstadoCanton → aspecto` | `{"Libre":"VíaLibre"}`; en maniobra `"MovimientoAutorizado"` | Aspecto máximo según la ocupación del cantón. Las claves válidas son `Libre`, `Prenormalizado`, `OcupadoMismoSentido` y `Ocupado`. |
| `LímiteProximidad` | array de referencias a CV | — | CVs que delimitan la proximidad de la señal. |
| `RutaNecesaria` | booleano | `false` para `Intermedia` y `Avanzada`; `true` para el resto | Exige una ruta formada para abrir la señal. |
| `ItinerariosDesviada` | booleano | `false` | Indica que la señal es de vía de apartado con todos los itinerarios a desviada. |

### Bloqueo (`Bloqueos`)

Cada bloqueo es un objeto de un array.

| Campo | Tipo | Por defecto | Descripción |
| --- | --- | --- | --- |
| `Colateral` | cadena | — | **Obligatorio.** Dependencia colateral. Junto con `Vía` forma el identificador del bloqueo: `<Dependencia>:<Colateral><Vía>`. |
| `Vía` | cadena | `""` | En vía doble o múltiple, distingue las vías entre las dos dependencias. |
| `Lado` | `Lado` | — | **Obligatorio.** Sentido del bloqueo emisor. |
| `Tipo` | `BAU`, `BAD`, `BAB`, `BLAU`, `BLAD` o `BLAB` | `BAU` | Modalidad de bloqueo. |
| `SentidoPreferente` | `Lado` | — | **Obligatorio** salvo en `BAU` y `BLAU`. Determina el estado inicial de `BAD` y `BLAD`, inhibe el desbloqueo automático en ese sentido. |
| `CVs` | array de referencias a secciones | — | **Obligatorio.** Secciones del trayecto de bloqueo, empezando por el más cercano a la estación. |
| `CVsEntrada` | entero | 1 | Número de CVs de entrada/agujas para detectar escapes de material. |

### Destino de ruta (`DestinosRuta`)

| Campo | Tipo | Descripción |
| --- | --- | --- |
| `Tipo` | `"Señal"`, `"Colateral"` o `"FinalVía"` | **Obligatorio.** Naturaleza del final de ruta. Para `Señal`, la clave debe coincidir con una señal local. |
| `Deslizamiento` | objeto | Solo para destino `Señal`; define el deslizamiento posterior. |

Objeto `Deslizamiento`:

| Campo | Tipo | Por defecto | Descripción |
| --- | --- | --- | --- |
| `Límite` | array de referencias a sección | - | **Obligatorio.** Secciones límite del recorrido de deslizamiento. |
| `DeslizamientosOrientados` | array de objetos `sección → [par, impar]` | deslizamiento libre únicamente | Cada objeto define una lista de aparatos que deben quedar orientados y enclavados para compatibilizar otra ruta. |

### Ruta (`Rutas`)

Cada entrada es un movimiento (itinerario, rebase o maniobra), definido por su `Inicio` y `Destino`.

| Campo | Tipo | Por defecto | Descripción |
| --- | --- | --- | --- |
| `Tipo` | `"Itinerario"`, `"Maniobra"` o `"Rebase"` | — | **Obligatorio.** Tipo de movimiento. |
| `Inicio` | id de señal | — | **Obligatorio.** Señal de inicio de ruta. |
| `Destino` | referencia a destino | — | **Obligatorio.** Clave de `DestinosRuta`. Puede ser una señal, colateral o final especial. |
| `Bloqueo` | referencia a bloqueo | — | Bloqueo de salida de la ruta. |
| `SecciónFin` | referencia a sección | — | Última sección reservada; el recorrido entre señal y ella se deduce de la topología y la posición de aparatos. Una ruta sin `SecciónFin` puede ser válida, pero no reserva secciones (para expedir directamente al bloqueo). |
| `PosiciónAparatos` | objeto `sección → [par, impar]` | — | Orienta y enclava el camino a través de agujas o secciones con varias salidas. |
| `Compatible` | compatibilidad | `"IncompatibleBloqueo"` | Compatibilidad de la maniobra con movimientos por la colateral: `Incompatible` (no se permiten maniobras con bloqueo receptor ni maniobras simultáneas), `IncompatibleBloqueo` (no se permiten maniobras con bloqueo receptor), `IncompatibleMovimiento` (se permite mantener bloqueo receptor siempre que no haya movimientos en la colateral), `IncompatibleItinerario` (se permite bloqueo receptor y maniobras en la colateral pero no itinerarios de salida) o `Compatible` (el bloqueo receptor y los movimientos en la colateral son compatibles). |
| `DiferímetroDAI1` | número | parámetro general; `0` en maniobra | Temporizador DAI zona 1. |
| `DiferímetroDAI2` | número | parámetro general | Temporizador DAI zona 2. |
| `DiferímetroDEI` | número | parámetro general | Temporizador DEI. |
| `DesactivarDiferímetroDAI` | booleano | `false` | Desactiva la temporización DAI. Su uso principal es en señales de salida para las que se necesita la señal de marche el tren. |
| `Deslizamiento` | cualquier valor/presencia | — | Su presencia hace que la sección final de rutas de maniobra o rebase admita ocupación; la definición del recorrido está en el destino. |
| `DiferímetroDeslizamiento` | objeto | — | Indica que el destino es un CV de estacionamiento. La ruta se desenclava de forma temporizada tras la ocupación del CV, sin ser necesaria su liberación. |
| `SeñalLiberación` | referencia a señal | — | Señal de salida al trayecto situada posteriormente a la última sección de la ruta, que requiere una ruta para su apertura. |
| `FAI` | booleano | `false` | Activa la formación automática de itinerario. |
| `TipoFAI` | `"Proximidad"` o `"Reserva"` | `"Proximidad"` | `"Proximidad"` lanza el FAI por ocupación de la proximidad, `"Reserva"` lo activa cuando existe final de movimiento en la señal (o bloqueo receptor). |

Campos de `DiferímetroDeslizamiento`:

| Campo | Tipo | Por defecto | Descripción |
| --- | --- | --- | --- |
| `InicioTemporizador` | referencia a sección | Último CV de la ruta | Indica la sección en la que se inicia el temporizador de liberación de ruta. |
| `Valor` | número | `30` | Duración del diferímetro. |

### Servicio intermitente (`ServicioIntermitente`)

| Campo | Tipo | Por defecto | Descripción |
| --- | --- | --- | --- |
| `Activo` | booleano | `false` | Inicia la dependencia cerrada en servicio intermitente. |
| `FAI` | array de ids de ruta | — | Rutas para las que se activa el FAI con estación cerrada. Los ids son los correspondientes al mando de ruta, por ejemplo `"I EST S1 F1"`. |
| `ItinerariosApertura` | array de ids de ruta | — | Rutas que se establecen al abrir la estación. |
| `PosiciónAparatos` | objeto `sección → [par, impar]` | — | Aparatos que se orientan y enclavan al cerrar la estación. |
| `Secciones` | objeto `sección → [entrada, salida]` | — | Secciones que quedan reservadas al cerrar la estación. |
| `SeñalesAbiertas` | objeto `señal → configuración` | — | Señales tratadas especialmente con la estación cerrada. |

Configuración de cada entrada de `SeñalesAbiertas`:

| Campo | Tipo | Por defecto | Descripción |
| --- | --- | --- | --- |
| `RutaNecesaria` | booleano | `false` | Permite la apertura de la señal sin movimiento establecido. |
| `Desbloqueo` | booleano | `false` | Permite apertura sin bloqueo establecido. |
| `BloqueoReceptor` | booleano | `false` | Permite la apertura de la señal independientemente del bloqueo. |
