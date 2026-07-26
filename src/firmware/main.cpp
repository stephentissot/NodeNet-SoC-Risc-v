#include <stdint.h>

// Bare-metal C++ stubs (no libstdc++, no exceptions, no RTTI)
// Called if a pure virtual function is invoked — should never happen in practice.
extern "C" void __cxa_pure_virtual() { while (1); }

#define LED (*(volatile uint32_t*)0x10000000)

#define UART0_DATA   (*(volatile uint32_t*)0x10001000)
#define UART0_STATUS (*(volatile uint32_t*)0x10001004)
#define UART0_BAUD   (*(volatile uint32_t*)0x10001008)

#define UART_STATUS_RX_FIFO_EMPTY    (1u << 0)
#define UART_STATUS_RX_FIFO_FULL     (1u << 1)
#define UART_STATUS_TX_FIFO_EMPTY    (1u << 2)
#define UART_STATUS_TX_FIFO_FULL     (1u << 3)
#define UART_STATUS_RX_OVERRUN       (1u << 4)
#define UART_STATUS_RX_FRAMEERR      (1u << 5)

void delay()
{
    for (volatile int i = 0; i < 100000; i++)
    {
    }
}

static void uart0_putc(uint8_t value)
{
    while ((UART0_STATUS & UART_STATUS_TX_FIFO_FULL) != 0)
    {
    }

    UART0_DATA = value;
}

static void uart0_puts(const char *text)
{
    while (*text)
    {
        if (*text == '\n')
            uart0_putc('\r');

        uart0_putc((uint8_t)*text);
        text++;
    }
}

int main(void)
{
    uint32_t led_state = 0;

    UART0_BAUD = 27;
    uart0_puts("nodenet_riscv " FIRMWARE_VERSION "\n");

    while (1)
    {
        LED = led_state;
        led_state ^= 1u;
        delay();

        while ((UART0_STATUS & UART_STATUS_RX_FIFO_EMPTY) == 0)
        {
            uint8_t value = (uint8_t)UART0_DATA;

            if (value == '\r')
                uart0_putc('\n');

            uart0_putc(value);
        }

        if (UART0_STATUS & (UART_STATUS_RX_OVERRUN | UART_STATUS_RX_FRAMEERR))
            UART0_STATUS = UART_STATUS_RX_OVERRUN | UART_STATUS_RX_FRAMEERR;
    }
}