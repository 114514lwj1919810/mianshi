#ifndef RS485_H
#define RS485_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RS485_MAX_FRAME_SIZE  256U

/* CMT8021 + Q1 + MAX485 provide isolated automatic half-duplex direction. */

void Rs485_Init(uint32_t baudrate);
uint8_t Rs485_Send(const uint8_t *data, uint16_t length);
uint16_t Rs485_Read(uint8_t *data, uint16_t capacity);
void Rs485_ClearRx(void);
uint8_t Rs485_IsTxBusy(void);

#ifdef __cplusplus
}
#endif

#endif
