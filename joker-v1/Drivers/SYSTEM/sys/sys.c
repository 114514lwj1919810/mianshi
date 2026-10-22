#include "./SYSTEM/sys/sys.h"

uint8_t get_icahce_sta(void) { return 0U; }
uint8_t get_dcahce_sta(void) { return 0U; }

void sys_nvic_set_vector_table(uint32_t baseaddr, uint32_t offset)
{
    SCB->VTOR = baseaddr | (offset & 0xFFFFFE00UL);
}

void sys_wfi_set(void) { __WFI(); }
void sys_intx_disable(void) { __disable_irq(); }
void sys_intx_enable(void) { __enable_irq(); }
void sys_msr_msp(uint32_t addr) { __set_MSP(addr); }
void sys_cache_enable(void) { }

uint8_t sys_stm32_clock_init(uint32_t plln, uint32_t pllm,
                             uint32_t pllp, uint32_t pllq)
{
    (void)plln;
    (void)pllm;
    (void)pllp;
    (void)pllq;
    SystemCoreClockUpdate();
    return (SystemCoreClock == 168000000U) ? 0U : 1U;
}
