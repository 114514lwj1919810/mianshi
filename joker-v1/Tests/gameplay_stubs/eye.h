#include <stdint.h>
typedef enum { EYE_COUNTER_SPIT_COIN=0 } Eye_Counter;
void Eye_ClearCounter(Eye_Counter c);
uint32_t Eye_TakeCounter(Eye_Counter c);
