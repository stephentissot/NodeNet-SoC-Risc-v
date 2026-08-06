#ifndef I2C0_H
#define I2C0_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bigsister.h"

/*
 * Bare-metal Wishbone I2C driver for wb_i2c.sv.
 *
 * The driver is written for the Alex Forencich i2c_master_wbs_8 core
 * exposed behind the wb_i2c wrapper. The peripheral is memory-mapped at
 * 0x10005000 and exposed as 32-bit MMIO registers with a 4-byte word stride.
 *
 * Safety with optimization:
 *   - every MMIO access is done through volatile pointers,
 *   - compiler barriers are inserted around each read/write,
 *   - polling loops never assume the compiler will re-read the register.
 */

#ifndef I2C0_BASE
#define I2C0_BASE 0x10005000u
#endif

#ifndef I2C0_TIMEOUT_LOOP
#define I2C0_TIMEOUT_LOOP 10u
#endif

#define I2C0_RELEASE_DELAY 80000u

/* Register offsets (4-byte stride from base). */
#define I2C0_REG_STATUS      0x00u
#define I2C0_REG_FIFO_STATUS        0x04u
#define I2C0_REG_ADDR        0x08u
#define I2C0_REG_CMD         0x0Cu
#define I2C0_REG_DATA        0x10u
#define I2C0_REG_PRESC_LO    0x18u
#define I2C0_REG_PRESC_HI    0x1Cu

/* Status bits. */
#define I2C0_STATUS_BUSY      (1u << 0)
#define I2C0_STATUS_BUS_CTRL  (1u << 1)
#define I2C0_STATUS_BUS_ACT   (1u << 2)
#define I2C0_STATUS_MISS_ACK  (1u << 3)

/* FIFO status bits. */
#define I2C0_FIFO_CMD_EMPTY   (1u << 0)
#define I2C0_FIFO_CMD_FULL    (1u << 1)
#define I2C0_FIFO_CMD_OVF     (1u << 2)
#define I2C0_FIFO_WR_EMPTY    (1u << 3)
#define I2C0_FIFO_WR_FULL     (1u << 4)
#define I2C0_FIFO_WR_OVF      (1u << 5)
#define I2C0_FIFO_RD_EMPTY    (1u << 6)
#define I2C0_FIFO_RD_FULL     (1u << 7)

/* Command bits. */
#define I2C0_CMD_START        (1u << 0)
#define I2C0_CMD_READ         (1u << 1)
#define I2C0_CMD_WRITE        (1u << 2)
#define I2C0_CMD_STOP         (1u << 4)

enum i2c0_result {
    I2C0_OK = 0,
    I2C0_NACK = 1,
    I2C0_TIMEOUT = 2,
    I2C0_FIFO_ERROR = 3
};

static inline uintptr_t i2c0_reg_addr(uint32_t reg)
{
    return (uintptr_t)I2C0_BASE + (uintptr_t)reg;
}

static inline uint32_t i2c0_reg_read32(uint32_t reg)
{
    volatile uint32_t *addr = (volatile uint32_t *)i2c0_reg_addr(reg);
    uint32_t value = *addr;
    __asm__ volatile("" ::: "memory");
    return value;
}

static inline void i2c0_reg_write32(uint32_t reg, uint32_t value)
{
    volatile uint32_t *addr = (volatile uint32_t *)i2c0_reg_addr(reg);
    *addr = value;
    __asm__ volatile("" ::: "memory");
}

static inline bool i2c0_nack_detected(void)
{
    return (i2c0_reg_read32(I2C0_REG_STATUS) & I2C0_STATUS_MISS_ACK) != 0u;
}

static inline void i2c0_clear_nack(void)
{
    i2c0_reg_write32(I2C0_REG_STATUS, I2C0_STATUS_MISS_ACK);
}

static inline bool i2c0_wait_idle(uint32_t timeout_ms)
{
    // Sécurité : si l'utilisateur passe 0 ou une valeur trop grande issue de l'ancienne macro, 
    // on force un timeout réaliste de 10 millisecondes.
    const uint32_t max_duration = (timeout_ms == 0u || timeout_ms > 1000u) ? 10u : timeout_ms;
    
    // -------------------------------------------------------------------------
    // ÉTAPE 1 : Attendre que la FIFO de commande soit totalement vide
    // -------------------------------------------------------------------------
    uint32_t start = millis();
    while (1) {
        // Lecture explicite : GCC est obligé de générer l'instruction 'lw' ici
        uint32_t fifo_stat = i2c0_reg_read32(I2C0_REG_FIFO_STATUS);
        
        if ((fifo_stat & I2C0_FIFO_CMD_EMPTY) != 0u) {
            break; // La FIFO est vide, on sort proprement
        }
        
        if ((millis() - start) >= max_duration) {
            return false; // Timeout atteint
        }
        
        // Le 'nop' force une instruction physique, la barrière fige l'ordre
        __asm__ volatile("nop" ::: "memory");
    }

    // -------------------------------------------------------------------------
    // ÉTAPE 2 : Attendre que la machine d'état (BUSY) repasse au repos
    // -------------------------------------------------------------------------
    start = millis(); // On réinitialise le chronomètre pour la seconde attente
    while (1) {
        uint32_t status = i2c0_reg_read32(I2C0_REG_STATUS);
        
        if ((status & I2C0_STATUS_BUSY) == 0u) {
            break; // L'IP est au repos, le bus est libre
        }
        
        if ((millis() - start) >= max_duration) {
            return false; // Timeout atteint
        }
        
        __asm__ volatile("nop" ::: "memory");
    }

    // Barrière finale avant de rendre la main au main()
    __asm__ volatile("" ::: "memory");
    return true;
}

static inline bool i2c0_push_data(uint8_t data)
{
    const uint32_t start = millis();
    while ((i2c0_reg_read32(I2C0_REG_FIFO_STATUS) & I2C0_FIFO_WR_FULL) != 0u) {
        if ((millis() - start) >= I2C0_TIMEOUT_LOOP) {
            return false;
        }
    }
    i2c0_reg_write32(I2C0_REG_DATA, (uint32_t)data);
    return true;
}

static inline bool i2c0_push_cmd(uint8_t cmd)
{
    const uint32_t start = millis();
    while ((i2c0_reg_read32(I2C0_REG_FIFO_STATUS) & I2C0_FIFO_CMD_FULL) != 0u) {
        if ((millis() - start) >= I2C0_TIMEOUT_LOOP) {
            return false;
        }
    }
    i2c0_reg_write32(I2C0_REG_CMD, (uint32_t)cmd);
    return true;
}

static inline void i2c0_init(uint16_t prescale)
{
    i2c0_reg_write32(I2C0_REG_PRESC_LO, (uint32_t)(prescale & 0xFFu));
    i2c0_reg_write32(I2C0_REG_PRESC_HI, (uint32_t)((prescale >> 8) & 0xFFu));
    i2c0_clear_nack();
    i2c0_reg_write32(I2C0_REG_CMD, I2C0_CMD_STOP);
    (void)i2c0_wait_idle(I2C0_TIMEOUT_LOOP);
}

static inline void i2c0_set_address(uint8_t addr)
{
    i2c0_reg_write32(I2C0_REG_ADDR, (uint32_t)addr);
}

static inline int i2c0_write(uint8_t addr, const uint8_t *buf, size_t len)
{
    if (len == 0u) {
        return I2C0_OK;
    }

    if (i2c0_nack_detected()) {
        i2c0_clear_nack();
    }
    i2c0_set_address(addr);
    if (len == 1u) {
        if (!i2c0_push_data(buf[0])) return I2C0_FIFO_ERROR;    
        asm volatile("" ::: "memory");
        if (!i2c0_push_cmd(I2C0_CMD_START | I2C0_CMD_WRITE)) return I2C0_FIFO_ERROR;

        // Attendre que la commande soit consommée avant de toucher à l'adresse pour le STOP
        const uint32_t wait_start = millis();
        while ((i2c0_reg_read32(I2C0_REG_FIFO_STATUS) & I2C0_FIFO_CMD_EMPTY) == 0u) {
            if ((millis() - wait_start) >= I2C0_TIMEOUT_LOOP) {
                return I2C0_TIMEOUT;
            }
        }
        asm volatile("" ::: "memory");
        if (!i2c0_push_cmd(I2C0_CMD_STOP)) return I2C0_FIFO_ERROR;

    } else {
        i2c0_set_address(addr);
        // --- Premier octet (START + WRITE) ---
        if (!i2c0_push_data(buf[0])) return I2C0_FIFO_ERROR;
        asm volatile("nop" ::: "memory");
        if (!i2c0_push_cmd(I2C0_CMD_START | I2C0_CMD_WRITE)) return I2C0_FIFO_ERROR;
        
        // --- Octets intermédiaires (len >= 3) ---
        for (size_t i = 1; i + 1u < len; ++i) {
            
            // CRUCIAL : Attendre que la FIFO de commande soit TOTALEMENT VIDE.
            // Cela prouve que l'IP a consommé l'adresse du cycle précédent, 
            // on peut maintenant modifier le registre d'adresse sans écraser le passé.
            const uint32_t wait_start = millis();
            while ((i2c0_reg_read32(I2C0_REG_FIFO_STATUS) & I2C0_FIFO_CMD_EMPTY) == 0u) {
                if ((millis() - wait_start) >= I2C0_TIMEOUT_LOOP) {
                    return I2C0_TIMEOUT;
                }
            }
            if (!i2c0_push_data(buf[i])) return I2C0_FIFO_ERROR;
            asm volatile("nop" ::: "memory");
            if (!i2c0_push_cmd(I2C0_CMD_WRITE)) return I2C0_FIFO_ERROR;
        }
        
        // --- Dernier octet (WRITE puis STOP séparé) ---
        // Attendre que la FIFO de commande soit vide avant de préparer le dernier WRITE
        const uint32_t wait_start = millis();
        while ((i2c0_reg_read32(I2C0_REG_FIFO_STATUS) & I2C0_FIFO_CMD_EMPTY) == 0u) {
            if ((millis() - wait_start) >= I2C0_TIMEOUT_LOOP) {
                return I2C0_TIMEOUT;
            }
        }

        if (!i2c0_push_data(buf[len - 1u])) return I2C0_FIFO_ERROR;
        
        i2c0_set_address(addr);
        asm volatile("nop" ::: "memory");
        if (!i2c0_push_cmd(I2C0_CMD_WRITE)) return I2C0_FIFO_ERROR;

        // Attendre à nouveau que la FIFO de commande se vide avant de poser le STOP
        const uint32_t stop_wait_start = millis();
        while ((i2c0_reg_read32(I2C0_REG_FIFO_STATUS) & I2C0_FIFO_CMD_EMPTY) == 0u) {
            if ((millis() - stop_wait_start) >= I2C0_TIMEOUT_LOOP) {
                return I2C0_TIMEOUT;
            }
        }

        i2c0_set_address(addr);
        asm volatile("nop" ::: "memory");
        if (!i2c0_push_cmd(I2C0_CMD_STOP)) return I2C0_FIFO_ERROR;
    }

    // Attente finale de libération complète du bus physique
    if (!i2c0_wait_idle(I2C0_TIMEOUT_LOOP)) {
        return I2C0_TIMEOUT;
    }

    if (i2c0_nack_detected()) {
        i2c0_clear_nack();
        return I2C0_NACK;
    }

    return I2C0_OK;
}

static inline int i2c0_read(uint8_t addr, uint8_t *buf, size_t len)
{
    if (len == 0u) {
        return I2C0_OK;
    }

    if (i2c0_nack_detected()) {
        i2c0_clear_nack();
    }

    i2c0_set_address(addr);

    for (size_t i = 0; i < len; ++i) {
        uint8_t cmd = (i == 0u) ? (I2C0_CMD_START | I2C0_CMD_READ) : I2C0_CMD_READ;
        if (i + 1u == len) {
            cmd |= I2C0_CMD_STOP;
        }
        if (!i2c0_push_cmd(cmd)) {
            return I2C0_FIFO_ERROR;
        }
    }

    for (size_t i = 0; i < len; ++i) {
        const uint32_t start = millis();
        while ((i2c0_reg_read32(I2C0_REG_FIFO_STATUS) & I2C0_FIFO_RD_EMPTY) != 0u) {
            if ((millis() - start) >= I2C0_TIMEOUT_LOOP) {
                return I2C0_TIMEOUT;
            }
        }
        buf[i] = (uint8_t)(i2c0_reg_read32(I2C0_REG_DATA) & 0xFFu);
    }

    if (!i2c0_wait_idle(I2C0_TIMEOUT_LOOP)) {
        return I2C0_TIMEOUT;
    }

    if (i2c0_nack_detected()) {
        i2c0_clear_nack();
        return I2C0_NACK;
    }

    return I2C0_OK;
}

static inline bool i2c0_probe(uint8_t addr)
{
    uint8_t dummy = 0u;
    for (int attempt = 0; attempt < 3; ++attempt) {
        i2c0_clear_nack();
        i2c0_reg_write32(I2C0_REG_CMD, I2C0_CMD_STOP);
        (void)i2c0_wait_idle(I2C0_TIMEOUT_LOOP);
        if (i2c0_write(addr, &dummy, 1u) == I2C0_OK) {
            return true;
        }
    }
    return false;
}

/* Compatibility aliases used by the existing firmware code. */
// static inline void i2c_init(uint16_t prescale) { i2c0_init(prescale); }
// static inline bool i2c_nack_detected(void) { return i2c0_nack_detected(); }
// static inline void i2c_clear_nack(void) { i2c0_clear_nack(); }
// static inline void i2c_set_address(uint8_t addr) { i2c0_set_address(addr); }
// static inline int i2c_write(uint8_t addr, const uint8_t *buf, uint8_t len) { return i2c0_write(addr, buf, (size_t)len); }
// static inline int i2c_read(uint8_t addr, uint8_t *buf, uint8_t len) { return i2c0_read(addr, buf, (size_t)len); }
// static inline bool i2c_probe(uint8_t addr) { return i2c0_probe(addr); }

#endif /* I2C0_H */
