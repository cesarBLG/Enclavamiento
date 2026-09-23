import re
import time
import threading
import paho.mqtt.client as mqtt
import json
import sys

# MQTT configuration
BROKER = "127.0.0.1"
PORT = 1883

# MQTT callbacks
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("mqtt: connected")
        client.subscribe("signal/+/+/state")
        client.subscribe("aguja/+/+/comprobacion")
        client.subscribe("simulador_trenes")
    else:
        print(f"Connection failed with code {rc}")

def id_to_mqtt(id):
    return id.replace('/','_').replace(':','/')
def id_from_mqtt(id):
    return id.replace('/',':').replace('_','/')
def opp_lado(lado):
    return "Impar" if lado == "Par" else "Par"

estado_agujas = dict()
estado_señales = dict()

secciones = dict()
cvs = dict()
trenes = dict()
class seccion:
    def __init__(self, id, jsec):
        self.id = id
        self.trenes = []
        self.señales = dict()
        self.conexiones = dict()
        if jsec.get("Tipo") == "Aguja":
            self.aguja = True
            self.lado = jsec["Lado"]
            self.conexiones[jsec["Lado"]] = jsec["SeccionesTalón"]
            self.conexiones[opp_lado(jsec["Lado"])] = [jsec["SecciónPunta"]]
        else:
            self.aguja = False
            self.conexiones = jsec["Conexiones"]
        cv = jsec.get("CV", id)
        if cv in cvs:
            self.cv = cvs[cv]
            self.cv.secciones.append(id)
    def ocupar_tren(self, tren):
        print(f'Tren {tren} ocupando {self.id}')
        self.trenes.append(tren)
        if self.cv:
            self.cv.update()
    def liberar_tren(self, tren):
        print(f'Tren {tren} liberando {self.id}')
        self.trenes.remove(tren)
        if self.cv:
            self.cv.update()
    def siguiente_seccion(self, dir, prev):
        pin = -1
        if self.aguja:
            if dir == self.lado:
                if self.id in estado_agujas:
                    if estado_agujas[self.id] == "0":
                        pin = 0
                    elif estado_agujas[self.id] == "1":
                        pin = 1
            else:
                pin = 0
        else:
            pin = 0
            for i,c in enumerate(self.conexiones[opp_lado(dir)]):
                if c["Id"] == prev:
                    pin = i
            if pin >= len(self.conexiones[dir]):
                pin = 0
        if pin < 0 or pin >= len(self.conexiones[dir]):
            return (None, dir)
        conex = self.conexiones[dir][pin]
        if conex.get("InvertirParidad"):
            dir = opp_lado(dir)
        return conex["Id"], dir
    def set_señal(self, id, dir, pin):
        for i,c in enumerate(self.conexiones[opp_lado(dir)]):
            if pin == i:
                self.señales[(c["Id"], dir)] = id
                break
    def señal_inicio(self, prev, dir):
        return self.señales.get((prev, dir))
class cv:
    def __init__(self, id):
        self.id = id
        self.ocupado = False
        self.secciones = []
    def update(self):
        ocupado = False
        for id in self.secciones:
            sec = secciones[id]
            if len(sec.trenes) > 0:
                ocupado = True
                break
        if self.ocupado != ocupado:
            client.publish(f'cv/{id_to_mqtt(self.id)}/field_state', json.dumps({"Estado": "Ocupado" if ocupado else "Libre"}))
            self.ocupado = ocupado
class tren:
    def __init__(self, num, dir, pos):
        self.num = num
        self.dir = dir
        self.posicion = pos
        self.prev = None
        self.inicio_posicion = time.time()
        self.tiempo_ocupacion = 5
        secciones[pos].ocupar_tren(num)
    def update(self):
        if time.time() - self.inicio_posicion < self.tiempo_ocupacion:
            return
        next = secciones[self.posicion].siguiente_seccion(self.dir, self.prev)
        if next[0] == None:
            return
        sig = secciones[next[0]].señal_inicio(self.posicion, next[1])
        if sig != None and (not sig in estado_señales or estado_señales[sig] == "Parada" or estado_señales[sig] == "desconexión"):
            return
        self.avance(next[0], next[1])
    def avance(self, seccion, dir):
        prev = self.posicion
        def delayed_free():
            time.sleep(self.tiempo_ocupacion / 3)
            secciones[prev].liberar_tren(self.num)
        threading.Thread(
            target=delayed_free,
            daemon=True
        ).start()
        self.posicion = seccion
        self.prev = prev
        self.dir = dir
        self.inicio_posicion = time.time()
        secciones[seccion].ocupar_tren(self.num)
def on_message(client, userdata, msg):
    match = re.compile(r"^signal/([a-zA-Z0-9_-]+)/([a-zA-Z0-9_'-]+)/state$").match(msg.topic)
    if match:
        dep, id = match.groups()
        estado_señales[id_from_mqtt(f'{dep}/{id}')] = json.loads(msg.payload)['Aspecto']
    match = re.compile(r"^aguja/([a-zA-Z0-9_-]+)/([a-zA-Z0-9_'-]+)/comprobacion$").match(msg.topic)
    if match:
        dep, id = match.groups()
        estado_agujas[id_from_mqtt(f'{dep}/{id}')] = msg.payload.decode('utf-8')
    if msg.topic == "simulador_trenes":
        cmd = msg.payload.decode('utf-8').split(' ')
        if len(cmd) < 2:
            return
        if cmd[0] == "T" and len(cmd) == 4:
            trenes[cmd[1]] = tren(cmd[1], cmd[2], cmd[3])
        elif cmd[0] == "D":
            tr = trenes.pop(cmd[1])
            secciones[tr.posicion].liberar_tren(tr.num)
# Create client
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, PORT)

cfg_dependencias = dict()
for filename in sys.argv[1:]:
    with open(filename, "r", encoding="utf-8") as f:
        data = json.load(f)
        for dep_id, dependencia in data["Dependencias"].items():
            if dependencia.get("Controlada") is False or not dependencia.get("CVs"):
                continue
            cfg_dependencias[dep_id] = dependencia

for dep_id, dependencia in cfg_dependencias.items():
    for id, jcv in dependencia["CVs"].items():
        full_id = f"{dep_id}:{id}"
        cvs[full_id] = cv(full_id)
for dep_id, dependencia in cfg_dependencias.items():
    for id, jsec in dependencia["Secciones"].items():
        full_id = f"{dep_id}:{id}"
        secciones[full_id] = seccion(full_id, jsec)
for dep_id, dependencia in cfg_dependencias.items():
    for id, jsig in dependencia["Señales"].items():
        full_id = f"{dep_id}:{id}"
        secciones[jsig["Sección"]].set_señal(full_id, jsig["Lado"], jsig.get("Pin", 0))

# Start loop
client.loop_start()
time.sleep(5)
while True:
    for tr in trenes.values():
        tr.update()
    time.sleep(5)
client.loop_stop()
