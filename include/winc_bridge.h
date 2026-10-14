#ifndef WINC_BRIDGE_H
#define WINC_BRIDGE_H

// "winc bridge": passes the WINC1500 flash through to the PC (scripts/co2ampel_tool.py), e.g. to manage
// the TLS root certificates. Same serial protocol as the WiFi101 FirmwareUpdater sketch; writes are
// limited to the root certificate sector. Stops WiFi and restarts the device when done (never returns).
void winc_bridge(void);

#endif
