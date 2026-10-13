#ifndef WINC_CERTS_H
#define WINC_CERTS_H

#include <Arduino.h>

// TLS root certificates in the WINC1500 flash (needed for MQTTS), managed from the running firmware.
// Every function stops WiFi (WINC download mode) and restarts the device when it is done.
// Store format: see scripts/winc_rootcert.py.

void winc_cert_list(Print &out);
void winc_cert_remove(Print &out, unsigned int index);

// "cert add": collects a pasted PEM certificate line by line.
void winc_cert_add_begin(Print &out);
bool winc_cert_add_active(void);
void winc_cert_add_line(Print &out, const char *line); // writes and restarts after the END line
void winc_cert_add_poll(Print &out);                   // aborts after 60 s without input

#endif
