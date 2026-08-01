/**
 * i2c.h — Driver MMIO pour wb_i2c / i2c_master_wbs_8
 *
 * Style inspiré de test.h (struct volatile) adapté aux registres réels du core
 * Alex Forencich i2c_master_wbs_8 exposé via wb_i2c.sv (stride 4 octets).
 *
 * Carte des registres (base = 0x10005000, stride 4 octets) :
 *   +0x00  status   R/W  [0]=busy  [3]=miss_ack (W1C)
 *   +0x04  fifo     R    [0]=cmd_empty [1]=cmd_full [4]=wr_full [6]=rd_empty
 *   +0x08  addr     W    [6:0] adresse 7 bits de l'esclave
 *   +0x0C  cmd      W    [0]=start [1]=read [2]=write [4]=stop
 *   +0x10  data     R/W  push TX FIFO / pop RX FIFO
 *   +0x14  (réservé)
 *   +0x18  presc_lo W    prescale[7:0]
 *   +0x1C  presc_hi W    prescale[15:8]
 *
 * Prescale = Fclk / (FI2C × 4)
 *   100 kHz @ 25 MHz → 62    400 kHz @ 25 MHz → 15
 */

#ifndef I2C_H
#define I2C_H

#include <stdint.h>
#include <stdbool.h>

/* ─── Structure de registres ─────────────────────────────────────────────── */

typedef struct {
    volatile uint32_t status;    /* +0x00  busy / NACK / état du bus          */
    volatile uint32_t fifo;      /* +0x04  occupation des FIFOs               */
    volatile uint32_t addr;      /* +0x08  adresse 7 bits de l'esclave        */
    volatile uint32_t cmd;       /* +0x0C  commande START/READ/WRITE/STOP     */
    volatile uint32_t data;      /* +0x10  push TX FIFO / pop RX FIFO         */
    volatile uint32_t _rsv;      /* +0x14  réservé                            */
    volatile uint32_t presc_lo;  /* +0x18  prescale bits [7:0]                */
    volatile uint32_t presc_hi;  /* +0x1C  prescale bits [15:8]               */
} I2C_Regs;

/* Accès typé : I2C(base)->champ */
#define I2C(base)  ((I2C_Regs *)(base))

/* ─── Bits status ────────────────────────────────────────────────────────── */
#define I2C_STAT_BUSY      (1u << 0)
#define I2C_STAT_MISS_ACK  (1u << 3)  /* NACK — écrire 1 pour effacer (W1C)  */

/* ─── Bits fifo ──────────────────────────────────────────────────────────── */
#define I2C_FIFO_CMD_EMPTY (1u << 0)
#define I2C_FIFO_CMD_FULL  (1u << 1)
#define I2C_FIFO_WR_FULL   (1u << 4)
#define I2C_FIFO_RD_EMPTY  (1u << 6)

/* ─── Bits cmd ───────────────────────────────────────────────────────────── */
#define I2C_CMD_START  (1u << 0)
#define I2C_CMD_READ   (1u << 1)
#define I2C_CMD_WRITE  (1u << 2)
#define I2C_CMD_STOP   (1u << 4)

/* ─── Timeout ────────────────────────────────────────────────────────────── */
#define I2C_TIMEOUT_LOOPS  200000u

/* ─── Primitives ─────────────────────────────────────────────────────────── */

static inline void i2c_init(uint32_t base, uint16_t prescale)
{
    I2C(base)->presc_lo = (uint32_t)(prescale & 0xFFu);
    I2C(base)->presc_hi = (uint32_t)((prescale >> 8) & 0xFFu);
}

static inline bool i2c_nack_detected(uint32_t base)
{
    return (I2C(base)->status & I2C_STAT_MISS_ACK) != 0u;
}

static inline void i2c_clear_nack(uint32_t base)
{
    I2C(base)->status = I2C_STAT_MISS_ACK;
}

/* Pousse un octet dans la TX FIFO (attend si pleine). False = timeout. */
static inline bool i2c_push_data(uint32_t base, uint8_t byte)
{
    uint32_t t = I2C_TIMEOUT_LOOPS;
    while ((I2C(base)->fifo & I2C_FIFO_WR_FULL) && --t) {}
    if (!t) return false;
    I2C(base)->data = (uint32_t)byte;
    return true;
}

/* Pousse une commande dans la CMD FIFO (attend si pleine). False = timeout. */
static inline bool i2c_push_cmd(uint32_t base, uint8_t cmd_val)
{
    uint32_t t = I2C_TIMEOUT_LOOPS;
    while ((I2C(base)->fifo & I2C_FIFO_CMD_FULL) && --t) {}
    if (!t) return false;
    I2C(base)->cmd = (uint32_t)cmd_val;
    return true;
}

/* Attend que la CMD FIFO soit vide puis que le bus soit inactif. */
static inline bool i2c_wait_idle(uint32_t base)
{
    uint32_t t = I2C_TIMEOUT_LOOPS;
    while (!(I2C(base)->fifo & I2C_FIFO_CMD_EMPTY) && --t) {}
    if (!t) return false;
    t = I2C_TIMEOUT_LOOPS;
    while ((I2C(base)->status & I2C_STAT_BUSY) && --t) {}
    return t > 0u;
}

/* ─── Opérations haut niveau ─────────────────────────────────────────────── */

/**
 * Écrit len octets vers l'adresse 7 bits addr.
 * Pour chaque octet : push DATA puis push CMD(WRITE).
 *   Premier octet → START|WRITE    Dernier octet → WRITE|STOP
 * Retourne : 0=OK  1=NACK  2=timeout
 */
static inline int i2c_write(uint32_t base, uint8_t addr,
                             const uint8_t *buf, uint8_t len)
{
    if (len == 0u) return 0;
    if (i2c_nack_detected(base)) i2c_clear_nack(base);

    I2C(base)->addr = (uint32_t)addr;

    for (uint8_t i = 0u; i < len; i++) {
        if (!i2c_push_data(base, buf[i])) return 2;

        uint8_t cmd_val = I2C_CMD_WRITE;
        if (i == 0u)         cmd_val |= I2C_CMD_START;
        if (i == len - 1u)   cmd_val |= I2C_CMD_STOP;

        if (!i2c_push_cmd(base, cmd_val)) return 2;
    }

    if (!i2c_wait_idle(base)) return 2;
    if (i2c_nack_detected(base)) { i2c_clear_nack(base); return 1; }
    return 0;
}

/**
 * Lit len octets depuis l'adresse 7 bits addr.
 * Retourne : 0=OK  1=NACK  2=timeout
 */
static inline int i2c_read(uint32_t base, uint8_t addr,
                            uint8_t *buf, uint8_t len)
{
    if (len == 0u) return 0;
    if (i2c_nack_detected(base)) i2c_clear_nack(base);

    I2C(base)->addr = (uint32_t)addr;

    for (uint8_t i = 0u; i < len; i++) {
        uint8_t cmd_val = I2C_CMD_READ;
        if (i == 0u)         cmd_val |= I2C_CMD_START;
        if (i == len - 1u)   cmd_val |= I2C_CMD_STOP;
        if (!i2c_push_cmd(base, cmd_val)) return 2;
    }

    for (uint8_t i = 0u; i < len; i++) {
        uint32_t t = I2C_TIMEOUT_LOOPS;
        while ((I2C(base)->fifo & I2C_FIFO_RD_EMPTY) && --t) {}
        if (!t) return 2;
        buf[i] = (uint8_t)(I2C(base)->data & 0xFFu);
    }

    if (!i2c_wait_idle(base)) return 2;
    if (i2c_nack_detected(base)) { i2c_clear_nack(base); return 1; }
    return 0;
}

/**
 * Sonde l'adresse 7 bits addr. Retourne true si l'esclave répond (ACK).
 */
static inline bool i2c_probe(uint32_t base, uint8_t addr)
{
    uint8_t dummy = 0x00u;
    return i2c_write(base, addr, &dummy, 1u) == 0;
}

#endif /* I2C_H */
