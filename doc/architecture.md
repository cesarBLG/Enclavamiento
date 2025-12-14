Circuito de vía (CV)
--------------------

Tramo de vía en el que se realiza la detección del tren. Un CV puede estar:
- Libre: sin presencia de tren
- Prenormalizado: no se tiene certeza sobre si el cantón está libre. En este estado,
  el cantón se libera con una secuencia correcta de ocupación y liberación.
- OcupadoImpar: ocupado por un tren en el sentido impar de circulación
- OcupadoPar: ocupado por un tren en el sentido par de circulación
- Ocupado: sentido de ocupación desconocido, u ocupado en ambos sentidos

La lógica del CV está gestionada por la clase *cv_impl*, que recibe mensajes del sistema
de detección en campo (contadores de ejes). El funcionamiento es el siguiente:
- El mensaje recibido de un contador de ejes incrementa o disminuye un contador
  que incluye el número de ejes en el CV.
- Dos mensajes se asignan al mismo tren si transcurren menos de 15 segundos entre ellos
- Se establece un temporizador que prenormaliza de forma automática el cantón tras la liberación
  de. al menos, el 50% de los ejes de cada tren.
- Se establece un temporizador que prenormaliza de forma automática e incondicional el cantón

Además, el operador puede efectuar una serie de mandos sobre el CV, de acuerdo con la normativa de referencia:
- LC: liberar (prenormalizar cantón)
- BTV: bloqueo de CV de trayecto
- DTV: anular bloqueo de CV de trayecto

Cada CV debe estar gestionado por un único enclavamiento, que deberá crear una clase *cv_impl*
por cada CV que gestione. El resto de enclavamientos podrán acceder al estado de dicho CV usando
la clase *cv*.

Sección de vía
--------------

Representa cada elemento individual que construye la topología de la vía, indicando las conexiones
con secciones de vía adyacentes. Puede corresponder a un CV lineal, una aguja, travesía, cruzamiento...
Como norma general, cada sección de vía estará protegida por un CV. Existirá un CV por cada
sección de vía, excepto en CV de agujas, que podrán abarcar varias agujas.

Por cada lado de la sección (par o impar), se asigna un número (pin) a cada punto de entrada a la sección.
La sección de vía asigna un pin de salida para cada pin de entrada. Algunos ejemplos:
- En una sección lineal, existe un único pin (pin 0) para cada lado, y están siempre conectados.
- En una aguja, existe un pin (pin 0) para un lado, y dos pines (pin 0 y pin 1) para el otro.
  Con la aguja a normal, ambos pin 0 están conectados, y el pin 1 está desconectado. Con la aguja
  a invertida, el pin 0 de un lado está conectado al pin 1 del otro lado, y el pin 0 está desconectado.
- En un cruzamiento, existen dos pines (pin 0 y pin 1) para cada lado. Los pines 0 están conectados entre
  sí, y lo mismo ocurre con los pines 1.

Las rutas permiten reservar una sección de vía para el paso de un tren, asegurando que no se establezcan
movimientos incompatibles.

Mandos aceptados:
- BIV: impedir establecimiento de movimientos por la sección
- DIV: permitir movimientos por la sección

Señal
-----

Dispositivo que permite o impide el paso de los trenes. Parámetros configurables:
- Ruta necesaria: indica que la señal solo abrirá si está establecido un itinerario o maniobra por la señal
- Cierre en stick: indica que si la señal se cierra por cualquier causa, no volverá a abrir hasta que
  se vuelva a mandar la ruta.
- Solicitud de apertura: si no se solicita apertura, la señal se cierra o se mantiene cerrada.
  Utilizado para implementar el mando de cierre de señal.

La clase *señal_impl* determina el aspecto de la señal, teniendo en cuenta la condiciones requeridas
por cada elemento del sistema para autorizar la apertura de señal. Cada señal debe estar gestionado
por un único enclavamiento, que deberá crear una clase *signal_impl* por cada señal que gestione.
El resto de enclavamientos podrán acceder al estado de dicha señal usando la clase *señal*.

Si el enclavamiento no permite la apertura de la señal, indicará parada. En caso contrario,
su aspecto se determina teniendo en cuenta el aspecto más restrictivo entre:
- El aspecto conforme al estado del cantón que protege. El estado del cantón viene determinado
  por el estado de los CV hasta la señal siguiente (en caso de maniobras, sólo se consideran
  los CV asegurados por la ruta). Esta tabla indica si se permiten varios trenes en el cantón,
  así como el aspecto máximo que puede indicar la señal.
- El aspecto máximo permitido por la señal siguiente. Por ejemplo, una señal en parada puede
  ordenar que la señal anterior muestre anuncio de parada.

Mandos aceptados:
- BS: bloqueo de señal, impedir establecimiento de rutas con origen en la señal
- ABS: anular bloqueo de señal
- DAI: disolución artificial de un itinerario con origen en la señal
- DAB: DAI con anulación de bloqueo emisor
- SA: establecer sucesión automática
- ASA: anular sucesión automática
- AFA: anular FAI
- CS: cierre de señal
- NPS (solo señales de trayecto): normalizar señal

Ruta
----

Los movimientos aseguran las secciones de vía que va a recorrer el tren, asegurando que no hay
movimientos incompatibles. Pueden ser de itinerario (para circulación de trenes) o maniobras
(en el ámbito de la estación). Las funciones de una ruta son:
- Establecimiento: comprueba que se cumplan las condiciones para autorizar la ruta. Los CV deben
  estar libres (u ocupados en el mismo sentido, si se permite), no debe haber rutas incompatibles
  ya establecidas, las secciones de vía a recorrer no están reservadas para otra ruta y el bloqueo
  no puede estar establecido en sentido contrario (salvo maniobras compatibles con el bloqueo).
- Disolución normal: conforme se produce la secuencia de ocupación y liberación de los CVs, las
  secciones de vía se van desenclavando, hasta que la ruta completa queda libre.
- Disolución artificial (DAI): ejecutada por el operador cuando no hay trenes en la ruta. Cierra la
  señal inmediatamente, pero la ruta no se libera hasta que transcurre un temporizador (diferímetro).
  Si la ruta se ocupa mientras actúa el temporizador, ésta no se libera. De esta forma, se mantiene
  la ruta asegurada si el tren no tuviera tiempo a reaccionar al cierre de la señal.
- Disolución de emergencia (DEI): ejecutada por el operador para disolver la ruta estando ocupada.
  Sólo se libera la ruta tras la actuación de un diferímetro.
- Sucesión automática (SA): desactiva el cierre en stick de las señales, y la ruta no se desenclava
  con el paso de la circulación. En cuanto se libera la ruta, la señal vuelve a abrir.
- Formación automática de itinerarios (FAI): establece la ruta de forma automática cuando un tren
  se acerca a la estación, siempre que la estación colateral autorice la apertura de la señal
  de salida. Si no se autorizan salidas (bloqueo prohibido o A/CTC denegada), no se establece el
  itinerario.

Los estados de una ruta pueden ser:
- Desenclavada: situación inicial, sin establecer
- Mandada: la ruta está mandada y se cumplen las condiciones para establecerla
- Formada: todos los elementos de la ruta están enclavados en su posición
- Supervisada: la señal de salida ha abierto, o una circulación ha rebasado la señal

Mandos:
- I: movimiento de itinerario
- R: movimiento de rebase
- M: maniobra
- ID: itinerario con formación diferida, establecer en cuanto se cumplan las condiciones
- FAI: establecer FAI

El origen de la rutas siempre es una señal. El destino puede ser la estación colateral, otra
señal o un final de vía. El destino de ruta acepta los siguientes mandos:
- BD: bloqueo de destino, impedir la formación de itinerarios con dicho destino
- ABD: anular bloqueo de destino
- DEI: establecer DEI para ruta establecida con dicho destino

Bloqueo
-------
Establece una relación de dependencia entre estaciones, para asegurar que la circulación por el
trayecto sólo se produce en un sentido en un momento determinado. El bloqueo se consigue por medio
de la comunicación entre los enclavamientos de ambas estaciones. Para establecer el bloqueo en
un sentido, o desbloquear el trayecto, se requiere que las condiciones de establecimiento o liberación
se cumplan en ambas estaciones, de la siguiente forma:
- Si se cumplen condiciones, la estación que quiere establecer/anular el bloqueo indica el estado
  objetivo a la colateral.
- Al recibir el estado objetivo, la estación colateral comprueba condiciones. Si se cumplen, considera
  el bloqueo en el estado solicitado por la colateral, y lo indica a la estación inicial.
- Cuando la estación inicial comprueba que la estación colateral ha aceptado la solicitud, considera
  cambia el bloqueo al nuevo estado que había solicitado.

Vinculación de bloqueos: en estaciones sin personal y/o con agujas talonables, se considera que los
bloqueos A y B con las estaciones colaterales están vinculados si un tren que entra por A sólo puede
ir a B y viceversa. Con esta funcionalidad, los bloqueos A y B actúan como un único bloqueo entre las
dos estaciones colaterales, realizándose la comunicación entre ambas a través de la estación donde
se produce la vinculación.

Mandos:
- B: tomar bloqueo
- AB: anular bloqueo y escape de material
- CSB: cierre de señales de bloqueo desde la estación receptora
- NSB: normalizar cierre de señales
- AS: autorización de salida al CTC. Permite la apertura de la señal de salida de la estación colateral
- AAS: anular autorización de salida al CTC. No permite la apertura de la señal de salida colateral
- PB: prohibir a la estación colateral la toma del bloqueo
- APB: anular prohibir bloqueo

Dependencia
-----------
Representa cada zona de mando controlada por el enclavamiento. El mando puede ser local o central (CTC).
El CTC puede tomar el mando en cualquier momento, los puestos locales solo si lo cede el CTC o por
emergencia. Solo se aceptan las órdenes del puesto de operaciones que tiene el mando.

Mandos:
- C: toma de mando por el CTC
- L: ceder el mando del CTC al puesto local
- TML: toma de mando local
- TME: toma de mando local por emergencia
- RML: ceder mando de un puesto local a otro
- CML: toma de mando local (sin autorización) desde un puesto local de mayor rango
- ME: mando especial. Permite confirmar la ejecución de órdenes no habituales con afectación a la seguridad.

Paso a nivel (PN)
-----------------
Gestiona la orden de cierre del PN. El PN se cierra si:
- El CV del PN está ocupado
- Existe una ruta establecida a través del PN con tren en su proximidad.
- Se establece el mando individual de cierre.

Cuando dejan de cumplirse todas las condiciones, el PN abre. También abre estando el CV
ocupado si el resto de condiciones de cierre no se cumplen tras finalizar un temporizador.

Para la apertura de señales en itinerario o rebase, se requiere comprobación de campo
de que el PN está cerrado.

Remota
------
Envía el estado del enclavamiento al CTC, y recibe las órdenes de mando.
