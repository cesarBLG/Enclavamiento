# Circuito de vía (CV)
Estructura: *comando* *dependencia* *id*
- LC: liberar (prenormalizar cantón)
- BTV: bloqueo de CV de trayecto
- DTV: anular bloqueo de CV de trayecto
- BIV: impedir establecimiento de movimientos por la sección
- DIV: permitir movimientos por la sección

# Señal
Estructura: *comando* *dependencia* *id*
- BS: bloqueo de señal, impedir establecimiento de rutas con origen en la señal
- ABS: anular bloqueo de señal
- DAI: disolución artificial de un itinerario con origen en la señal
- DAB: DAI con anulación de bloqueo emisor
- SA: establecer sucesión automática
- ASA: anular sucesión automática
- AFA: anular FAI
- CS: cierre de señal
- NPS (solo señales de trayecto): normalizar señal

# Ruta
Estructura: *comando* *dependencia* *id_inicio* *id_fin*
- I: movimiento de itinerario
- R: movimiento de rebase
- M: maniobra
- ID: itinerario con formación diferida, establecer en cuanto se cumplan las condiciones
- FAI: establecer FAI

# Fin de ruta
Estructura: *comando* *dependencia* *id*
- BD: bloqueo de destino, impedir la formación de itinerarios con dicho destino
- ABD: anular bloqueo de destino
- DEI: establecer DEI para ruta establecida con dicho destino

# Bloqueo
Estructura: *comando* *dependencia_emisora* *dependencia_receptora*\[*via*\]
- B: tomar bloqueo
- AB: anular bloqueo y escape de material
- CSB: cierre de señales de bloqueo desde la estación receptora
- NSB: normalizar cierre de señales
- AS: autorización de salida al CTC. Permite la apertura de la señal de salida de la estación colateral
- AAS: anular autorización de salida al CTC. No permite la apertura de la señal de salida colateral
- PB: prohibir a la estación colateral la toma del bloqueo
- APB: anular prohibir bloqueo

# Dependencia
Estructura: *comando* *dependencia* \[*puesto_cedido*\]
- C: toma de mando por el CTC
- L: ceder el mando del CTC al puesto local
- TML: toma de mando local
- TME: toma de mando local por emergencia
- RML: ceder mando de un puesto local a otro
- CML: toma de mando local (sin autorización) desde un puesto local de mayor rango
- ME: mando especial. Permite confirmar la ejecución de órdenes no habituales con afectación a la seguridad.

# Paso a Nivel (PN)
Estructura: *comando* *dependencia* *id*
- CPN: cerrar paso a nivel
- APN: abrir paso a nivel
