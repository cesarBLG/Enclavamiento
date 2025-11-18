#pragma once
#include <estado_remota.h>
#include <string>
void remota_cambio_elemento(ElementoRemota el, std::string id);
void remota_sendall();
void update_remota();
void handle_message_fec(const std::string client, const json &j);
