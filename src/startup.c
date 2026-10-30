// Cortex-M3 startup code.
//
// On reset, the core reads the initial stack pointer and reset handler from
// the first two entries at address 0x00000000. Reset_Handler then prepares the
// C runtime by copying .data to RAM, clearing .bss, and calling main().
#include <stdint.h>

extern uint32_t _estack;                 // Top of SRAM, defined by the linker.
extern uint32_t _sidata;                 // Initial .data values in flash.
extern uint32_t _sdata, _edata;          // .data destination range in RAM.
extern uint32_t _sbss, _ebss;            // .bss range in RAM.

int main(void);
void Reset_Handler(void);
void Default_Handler(void);

void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));

// Core exception vectors.
// The table currently has no peripheral IRQ entries because the firmware is
// polling-based. Add IRQ handlers after the core exceptions when interrupts
// are enabled, for example UART0 IRQ 5 at vector index 16 + 5.
__attribute__((section(".isr_vector"), used))
const uintptr_t vector_table[16] = {
    (uintptr_t)&_estack,           // 0: Initial stack pointer.
    (uintptr_t)Reset_Handler,      // 1: Reset.
    (uintptr_t)NMI_Handler,        // 2: Non-maskable interrupt.
    (uintptr_t)HardFault_Handler,  // 3: Hard fault.
    (uintptr_t)MemManage_Handler,  // 4: Memory management fault.
    (uintptr_t)BusFault_Handler,   // 5: Bus fault.
    (uintptr_t)UsageFault_Handler, // 6: Usage fault.
    0,
    0,
    0,
    0,                             // 7-10: Reserved.
    (uintptr_t)SVC_Handler,        // 11: Supervisor call.
    (uintptr_t)DebugMon_Handler,   // 12: Debug monitor.
    0,                             // 13: Reserved.
    (uintptr_t)PendSV_Handler,     // 14: Pendable service request.
    (uintptr_t)SysTick_Handler,    // 15: System tick.
};

void Reset_Handler(void)
{
  uint32_t *src = &_sidata;

  for (uint32_t *dst = &_sdata; dst < &_edata;) {
    *dst++ = *src++;
  }

  for (uint32_t *dst = &_sbss; dst < &_ebss;) {
    *dst++ = 0u;
  }

  (void)main();
  for (;;) {
  }
}

void Default_Handler(void)
{
  for (;;) {
  }
}
