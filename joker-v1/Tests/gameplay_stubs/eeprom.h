#ifndef JOKER_TEST_EEPROM_H
#define JOKER_TEST_EEPROM_H
#include <stdint.h>
uint8_t Eeprom_IsReady(void);
uint8_t Eeprom_Read(uint16_t a, uint8_t *p, uint16_t n);
uint8_t Eeprom_Write(uint16_t a, const uint8_t *p, uint16_t n);
#ifdef EEPROM_GUARD_TEST_EXTERNAL
uint8_t Eeprom_GetFault(void);
void Eeprom_ProtectData(uint16_t address);
#else
static uint8_t Eeprom_GetFault(void) { return 0U; }
static void Eeprom_ProtectData(uint16_t address) { (void)address; }
#endif
#endif
