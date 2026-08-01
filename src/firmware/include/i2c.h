#ifndef I2C_H
#define I2C_H

#include <stdint.h>
#include <stdbool.h>

/* ─── Registre I2C : Struct volatile avec accès direct ─────────────────── */
typedef volatile struct {
    uint32_t status;      /* +0x00: Status (BUSY, ..., MISS_ACK) */
    uint32_t fifo;        /* +0x04: FIFO status (CMD_EMPTY, CMD_FULL, ...) */
    uint32_t addr;        /* +0x08: I2C target address (7-bit) */
    uint32_t cmd;         /* +0x0C: Command reg (START, READ, WRITE, STOP) */
    uint32_t data;        /* +0x10: Data reg */
    uint32_t _rsv1;       /* +0x14: Reserved */
    uint32_t presc_lo;    /* +0x18: Prescale lo byte */
    uint32_t presc_hi;    /* +0x1C: Prescale hi byte */
} I2C_Regs;

/* ─── Pointeur constant vers la base I2C (hardcoded @ 0x10005000) ────────── */
#define I2C0  ((I2C_Regs * const)(0x10005000UL))

/* ─── Bits status ────────────────────────────────────────────────────────── */
#define I2C_STAT_BUSY      (1u << 0)
#define I2C_STAT_MISS_ACK  (1u << 3)  /* NACK — écrire 1 pour effacer (W1C)  */

/* ─── Bits fifo ──────────────────────────────────────────────────────────── */
#define I2C_FIFO_CMD_EMPTY (1u << 0)
#define I2C_FIFO_CMD_FULL  (1u << 1)
#define I2C_FIFO_WR_FULL   (1u << 4)
#define I2C_FIFO_RD_EMPTY  (1u << 6)
#define I2C_FIFO_RD_FULL   (1u << 7)

/* ─── Bits cmd ───────────────────────────────────────────────────────────── */
#define I2C_CMD_START  (1u << 0)
#define I2C_CMD_READ   (1u << 1)
#define I2C_CMD_WRITE  (1u << 2)
#define I2C_CMD_STOP   (1u << 4)

/* ─── Timeout de sécurité pour éviter le gel du Picorv32 ────────────────── */
#define I2C_TIMEOUT_LOOPS  800000u

/* ─── Primitives de bas niveau sécurisées ────────────────────────────────── */

static inline void i2c_init(uint16_t prescale)
{
    I2C0->presc_lo = (uint32_t)(prescale & 0xFFu);
    I2C0->presc_hi = (uint32_t)((prescale >> 8) & 0xFFu);
    
    if ((I2C0->status & I2C_STAT_MISS_ACK) != 0u) {
        I2C0->status = I2C_STAT_MISS_ACK;
    }
}

static inline bool i2c_nack_detected(void)
{
    return (I2C0->status & I2C_STAT_MISS_ACK) != 0u;
}

static inline void i2c_clear_nack(void)
{
    I2C0->status = I2C_STAT_MISS_ACK;
}

static inline bool i2c_push_data(uint8_t byte)
{
    uint32_t t = I2C_TIMEOUT_LOOPS;
    while (((I2C0->fifo & I2C_FIFO_WR_FULL) != 0u) && --t) { 
        asm volatile("nop"); 
    }
    if (!t) return false;
    I2C0->data = (uint32_t)byte;
    return true;
}

static inline bool i2c_push_cmd(uint8_t cmd_val)
{
    uint32_t t = I2C_TIMEOUT_LOOPS;
    while (((I2C0->fifo & I2C_FIFO_CMD_FULL) != 0u) && --t) { 
        asm volatile("nop"); 
    }
    if (!t) return false;
    I2C0->cmd = (uint32_t)cmd_val;
    return true;
}

static inline bool i2c_wait_busy(void)
{
    uint32_t t = I2C_TIMEOUT_LOOPS;
    /* Wait for FIFO to drain — force re-read each iteration */
    while ((I2C0->fifo & I2C_FIFO_CMD_EMPTY) == 0u && --t) {
        asm volatile(""); /* Prevent optimization */
    }
    if (!t) return false;
    
    t = I2C_TIMEOUT_LOOPS;
    /* Wait for bus to idle — force re-read each iteration */
    volatile uint32_t stat;
    while ((stat = I2C0->status, (stat & I2C_STAT_BUSY)) && --t) {
        asm volatile(""); /* Prevent optimization */
    }
    return t > 0u;
}

/* ─── Opérations haut niveau (Votre logique validée) ─────────────────────── */

/**
 * Écrit len octets vers l'adresse 7 bits addr.
 * Retourne : 0=OK  1=NACK  2=timeout
 */
static inline int i2c_write(uint8_t addr, const uint8_t *buf, uint8_t len)
{
    if (len == 0u) return 0;
    if (i2c_nack_detected()) i2c_clear_nack();

    I2C0->addr = (uint32_t)addr;

    if (len == 1u) {
        if (!i2c_push_data(buf[0])) return 2;
        if (!i2c_push_cmd(I2C_CMD_START | I2C_CMD_WRITE | I2C_CMD_STOP)) return 2;
    } else {
        // Premier octet : START
        if (!i2c_push_data(buf[0])) return 2;
        if (!i2c_push_cmd(I2C_CMD_START | I2C_CMD_WRITE)) return 2;
        
        // Octets intermédiaires
        for (uint8_t i = 1u; i < len - 1u; i++) {
            if (!i2c_push_data(buf[i])) return 2;
            if (!i2c_push_cmd(I2C_CMD_WRITE)) return 2;
            
            // Sécurité : Si l'esclave renvoie un NACK au milieu, on s'arrête
            if (i2c_nack_detected()) { i2c_clear_nack(); return 1; }
        }
        
        // Dernier octet : STOP
        if (!i2c_push_data(buf[len - 1u])) return 2;
        if (!i2c_push_cmd(I2C_CMD_WRITE | I2C_CMD_STOP)) return 2;
    }

    if (!i2c_wait_busy()) return 2;
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

    I2C0->addr = (uint32_t)addr;

    for (uint8_t i = 0u; i < len; i++) {
        uint8_t cmd_val = I2C_CMD_READ;
        if (i == 0u)        cmd_val |= I2C_CMD_START;
        if (i == len - 1u)  cmd_val |= I2C_CMD_STOP;
        
        if (!i2c_push_cmd(cmd_val)) return 2;

        uint32_t t = I2C_TIMEOUT_LOOPS;
        while (((I2C0->fifo & I2C_FIFO_RD_EMPTY) != 0u) && --t) { 
            asm volatile("nop"); 
        }
        if (!t) return 2;

        buf[i] = (uint8_t)(I2C0->data & 0xFFu);

        if (i2c_nack_detected()) { i2c_clear_nack(); return 1; }
    }

    if (!i2c_wait_busy()) return 2;
    return 0;
}

/**
 * Sonde l'adresse 7 bits addr. Retourne true si l'esclave répond (ACK).
 */
static inline bool i2c_probe(uint8_t addr)
{
    uint8_t dummy = 0x00u;
    return i2c_write(addr, &dummy, 1u) == 0;
}

#endif /* I2C_H */