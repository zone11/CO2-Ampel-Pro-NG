#ifndef SETTINGS_STORAGE_H
#define SETTINGS_STORAGE_H

#include "app_types.h"

void settings_read(SETTINGS *data);
void settings_write(const SETTINGS *data);

#endif
