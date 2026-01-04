#include "settings_storage.h"

#include <Arduino.h>

// Settings stored at high address in flash
// WARNING: Settings will be lost on firmware upload (bootloader erases entire app area)
// Recommended: Export settings via serial before updating firmware
// Place at 0x3F800 (last 2KB of flash) - may survive small firmware updates
#define SETTINGS_FLASH_ADDR ((const volatile SETTINGS*)0x0003F800)

// Liest Einstellungen aus Flash
void settings_read(SETTINGS *data)
{
  memcpy(data, (const void*)SETTINGS_FLASH_ADDR, sizeof(SETTINGS));
}

// Schreibt Einstellungen in Flash
void settings_write(const SETTINGS *data)
{
  // SAMD21 Flash-Programmierung
  // Page-Groesse: 64 Bytes (Row = 256 Bytes = 4 Pages)

  const uint32_t flash_addr = (uint32_t)SETTINGS_FLASH_ADDR;
  const uint32_t page_size = 64;
  const uint32_t row_size = 256;
  const uint32_t num_rows = (sizeof(SETTINGS) + row_size - 1) / row_size;

  // Disable interrupts during flash operations
  __disable_irq();

  // Erase rows first
  for(uint32_t i = 0; i < num_rows; i++)
  {
    uint32_t addr = flash_addr + (i * row_size);

    // Execute "Erase Row" command
    NVMCTRL->ADDR.reg = addr / 2; // Address must be divided by 2
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_ER;
    while(!NVMCTRL->INTFLAG.bit.READY);
  }

  // Write data page by page
  const uint32_t *src = (const uint32_t *)data;
  uint32_t num_pages = (sizeof(SETTINGS) + page_size - 1) / page_size;

  for(uint32_t page = 0; page < num_pages; page++)
  {
    // Clear page buffer
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_PBC;
    while(!NVMCTRL->INTFLAG.bit.READY);

    // Fill page buffer (16 words = 64 bytes per page)
    uint32_t *dst = (uint32_t *)(flash_addr + (page * page_size));
    for(uint32_t i = 0; i < (page_size / 4); i++)
    {
      dst[i] = *src++;
    }

    // Execute "Write Page" command
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_WP;
    while(!NVMCTRL->INTFLAG.bit.READY);
  }

  // Re-enable interrupts
  __enable_irq();
}
