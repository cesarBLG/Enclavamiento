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
        client.subscribe("pn/+/+/cierre")
        client.subscribe("aguja/+/+/mando")
    else:
        print(f"Connection failed with code {rc}")

def send_later(topic, msg, delay):
    def delayed_publish():
        time.sleep(delay)
        client.publish(topic, msg)
    threading.Thread(
        target=delayed_publish,
        daemon=True
    ).start()

def on_message(client, userdata, msg):
    topic = msg.topic
    #print(f"{topic}: {msg.payload}")

    match = re.compile(r"^pn/([a-zA-Z0-9_-]+)/([a-zA-Z0-9_'-]+)/cierre$").match(topic)
    if match:
        dep, id = match.groups()
        #client.publish(f"pn/{dep}/{id}/comprobacion", "")
        send_later(f"pn/{dep}/{id}/comprobacion", msg.payload, 3)
    match = re.compile(r"^aguja/([a-zA-Z0-9_-]+)/([a-zA-Z0-9_'-]+)/mando$").match(topic)
    if match:
        dep, id = match.groups()
        client.publish(f"aguja/{dep}/{id}/comprobacion", "")
        send_later(f"aguja/{dep}/{id}/comprobacion", msg.payload, 3)

# Create client
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, PORT)

cejes = []
cvs = []
agujas = []
for filename in sys.argv[1:]:
    with open(filename, "r", encoding="utf-8") as f:
        data = json.load(f)
        for dep_id, dependencia in data["Dependencias"].items():
            if dependencia.get("Controlada") is False or not dependencia.get("CVs"):
                continue
            for id, cv in dependencia["CVs"].items():
                if cv.get("ContadoresEjes"):
                    for cejes_id in cv["ContadoresEjes"].keys():
                        if cejes_id not in cejes:
                            cejes.append(cejes_id.replace('/', '_').replace(':','/'))
                else:
                    cvs.append(f"{dep_id}/{id}")
            for id, sec in dependencia["Secciones"].items():
                if sec.get("Tipo") == "Aguja":
                    agujas.append(f"{dep_id}/{id}")
# Start loop
client.loop_start()
time.sleep(2)
for id in cvs:
    client.publish(f'cv/{id}/field_state', json.dumps({"Estado": "Libre"}))
for id in agujas:
    client.publish(f"aguja/{id}/comprobacion", "0")
while True:
    for id in cejes:
        client.publish(f'cejes/{id}/event', "conexion")
    time.sleep(30)
client.loop_stop()
