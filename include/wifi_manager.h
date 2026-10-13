#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "app_globals.h"

void print_wifi_status(void);
void print_ip_address_line(const IPAddress &ip);
unsigned int wifi_start_ap(void);
unsigned int wifi_start(void);

#endif
