#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "app_globals.h"

void print_wifi_status(void);
void print_ip_address_line(const IPAddress &ip);
unsigned int wifi_start_ap(void);
unsigned int wifi_start(void);

// Device ID from the MAC address (full MAC, 12 hex digits)
void get_device_id(char *buffer, size_t buffer_size);
// System name: sys.name, or PRODUCT_NAME + " " + device ID when empty
const char *system_name(void);
// System name as DHCP hostname: [A-Za-z0-9-] only, other characters become '-', max 32 chars
void system_hostname(char *buffer, size_t buffer_size);

#endif
