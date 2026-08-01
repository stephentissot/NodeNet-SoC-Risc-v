#ifndef I2C_H
#define I2C_H

#include <stdint.h>
#include <stdbool.h>

/* ─── Adresse matérielle (en dur — une seule instance I2C dans ce SoC) ───── */
#define I2C0_BASE  0x10005000UL

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

/* Pointeur unique vers le périphérique — GCC émet directement lui+addi */
#define I2C_IP  ((I2C_Regs *)I2C0_BASE)

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
#define I2C_TIMEOUT_LOOPS  500000u

/* ─── Primitives ─────────────────────────────────────────────────────────── */

static inline void i2c_init(uint16_t prescale)
{
    I2C_IP->presc_lo = (uint32_t)(prescale & 0xFFu);
    I2C_IP->presc_hi = (uint32_t)((prescale >> 8) & 0xFFu);
}

static inline bool i2c_nack_detected(void)
{
    return (I2C_IP->status & I2C_STAT_MISS_ACK) != 0u;
}

static inline void i2c_clear_nack(void)
{
    I2C_IP->status = I2C_STAT_MISS_ACK;
}

static inline bool i2c_push_data(uint8_t byte)
{
    uint32_t t = I2C_TIMEOUT_LOOPS;
    while ((I2C_IP->fifo & I2C_FIFO_WR_FULL) && --t) { asm volatile("nop"); }
    if (!t) return false;
    I2C_IP->data = (uint32_t)byte;
    return true;
}

static inline bool i2c_push_cmd(uint8_t cmd_val)
{
    uint32_t t = I2C_TIMEOUT_LOOPS;
    while ((I2C_IP->fifo & I2C_FIFO_CMD_FULL) && --t) { asm volatile("nop"); }
    if (!t) return false;
    I2C_IP->cmd = (uint32_t)cmd_val;
    return true;
}

static inline bool i2c_wait_idle(void)
{
    uint32_t t = I2C_TIMEOUT_LOOPS;
    while (!(I2C_IP->fifo & I2C_FIFO_CMD_EMPTY) && --t) { asm volatile("nop"); }
    if (!t) return false;
    t = I2C_TIMEOUT_LOOPS;
    while ((I2C_IP->status & I2C_STAT_BUSY) && --t) { asm volatile("nop"); }
    return t > 0u;
}

/* ─── Opérations haut niveau ─────────────────────────────────────────────── */

/**
 * Écrit len octets vers l'adresse 7 bits addr.
 * Retourne : 0=OK  1=NACK  2=timeout
 */
static inline int i2c_write(uint8_t addr, const uint8_t *buf, uint8_t len)
{
    if (len == 0u) return 0;
    if (i2c_nack_detected()) i2c_clear_nack();

    I2C_IP->addr = (uint32_t)addr;

    for (uint8_t i = 0u; i < len; i++) {
        if (!i2c_push_data(buf[i])) return 2;

        uint8_t cmd_val = I2C_CMD_WRITE;
        if (i == 0u)        cmd_val |= I2C_CMD_START;
        if (i == len - 1u)  cmd_val |= I2C_CMD_STOP;

        if (!i2c_push_cmd(cmd_val)) return 2;

        /* Arrêt immédiat si NACK reçu en cours de transfert */
        if (i2c_nack_detected()) { i2c_clear_nack(); return 1; }
    }

    if (!i2c_wait_idle()) return 2;
    if (i2c_nack_detected()) { i2c_clear_nack(); return 1; }
    return 0;
}

/**
 * Lit len octets depuis l'adresse 7 bits addr.
 * Retourne : 0=OK  1=NACK  2=timeout
 */
static inline int i2c_read(uint8_t addr, uint8_t *buf, uint8_t len)
{
    if (len == 0u) return 0;
    if (i2c_nack_detected()) i2c_clear_nack();

    I2C_IP->addr = (uint32_t)addr;

    for (uint8_t i = 0u; i < len; i++) {
        uint8_t cmd_val = I2C_CMD_READ;
        if (i == 0u)        cmd_val |= I2C_CMD_START;
        if (i == len - 1u)  cmd_val |= I2C_CMD_STOP;
        if (!i2c_push_cmd(cmd_val)) return 2;

        uint32_t t = I2C_TIMEOUT_LOOPS;
        while ((I2C_IP->fifo & I2C_FIFO_RD_EMPTY) && --t) { asm volatile("nop"); }
        if (!t) return 2;

        buf[i] = (uint8_t)(I2C_IP->data & 0xFFu);

        if (i2c_nack_detected()) { i2c_clear_nack(); return 1; }
    }

    if (!i2c_wait_idle()) return 2;
    return 0;
}

/**
 * Sonde l'adresse 7 bits addr. Retourne true si l'esclave répond (ACK).
 * NOTE : pousse 0x00 dans TX FIFO — le wbs_8 exige un octet pour START|WRITE.
 */
static inline bool i2c_probe(uint8_t addr)
{
    if (i2c_nack_detected()) i2c_clear_nack();
    I2C_IP->addr = (uint32_t)addr;
    if (!i2c_push_data(0x00u)) return false;
    if (!i2c_push_cmd(I2C_CMD_START | I2C_CMD_WRITE | I2C_CMD_STOP)) return false;
    if (!i2c_wait_idle()) return false;
    if (i2c_nack_detected()) { i2c_clear_nack(); return false; }
    return true;
}

#endif /* I2C_H */