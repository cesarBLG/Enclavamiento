#pragma once
#include <estado_remota.h>
#include <string>
#include "id_elemento.h"
void remota_cambio_elemento(std::string tipo, const id_elemento &id);
void remota_sendall();
void update_remota();
void handle_message_fec(const std::string client, const json &j);
