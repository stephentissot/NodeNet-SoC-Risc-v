#ifndef I2C_MASTER_REGS_H
#define I2C_MASTER_REGS_H

#include <stdint.h>

#define I2C_MASTER_BASE 0x10005000UL

/* ─── Offsets des Registres en octets (Stride de 4 octets pour le CPU) ──── */
#define I2C_MASTER_CMD      0x00u  // Offset 0 -> Index de mot 0
#define I2C_MASTER_SPD      0x04u  // Offset 4 -> Index de mot 1
#define I2C_ISR             0x08u  // Offset 8 -> Index de mot 2
#define I2C_IEN             0x0Cu  // Offset 12 -> Index de mot 3
#define I2C_MASTER_MEM_BASE 0x80u  // Offset 128 (0x80) -> mot local 0x20 : zone mémoire du cœur ZipCPU
#define I2C_MASTER_MEM_SIZE_BYTES 112 // 112 octets de RAM proxy disponible (128 - 16)

/* ─── Masques du Registre de Commande Officiel de ZipCPU ───────────────── */
#define I2C_MASTER_CMD_BUSY 0x80000000u  /* Bit 31 (R) : 1 = Bus occupé */
#define I2C_MASTER_CMD_ERR  0x40000000u  /* Bit 30 (R) : 1 = Erreur / NACK */

// Adresse esclave I2C (Bits 23 à 17)
#define I2C_MASTER_CMD_SLV_ADDR_OFFSET     17u
#define I2C_MASTER_CMD_SLV_ADDR_MASK       0x00FE0000u

// Bit de Lecture / Écriture (Bit 16)
#define I2C_MASTER_CMD_RD                  0x00010000u  /* 1 = Lecture */
#define I2C_MASTER_CMD_WR                  0x00000000u  /* 0 = Écriture pure */

#define I2C_MASTER_CMD_START_ADDR_OFFSET   8u
#define I2C_MASTER_CMD_START_ADDR_MASK     0x00007F00u

#define I2C_MASTER_CMD_NUM_BYTES_OFFSET    0u
#define I2C_MASTER_CMD_NUM_BYTES_MASK      0x0000007Fu

#define I2C_MASTER_CMD_NUM_BYTES_OFFSET    0u
#define I2C_MASTER_CMD_NUM_BYTES_MASK      0x0000007Fu

/* ─── Masques du Registre de Vitesse (I2C_MASTER_SPD) ──────────────────── */
#define I2C_MASTER_SPD_MASK                0x000FFFFFu

/* ─── Masques des Interruptions (ISR / IEN) ────────────────────────────── */
#define I2C_ISR_BUSY                       0x00000001u  // <─── RÉINTÉGRÉ : Bit 0 (Busy -> Idle transition)
#define I2C_IEN_BUSY                       0x00000001u  // <─── RÉINTÉGRÉ : Bit 0 (Autorise l'IRQ correspondante)

/* Primitives d'accès directes sécurisées pour GCC -Os */
inline void i2c_master_reg_wr(uint32_t base, uint32_t reg_offset, uint32_t data) {
    *(volatile uint32_t *)(base + reg_offset) = data;
    __asm__ volatile("" ::: "memory");
}

inline uint32_t i2c_master_reg_rd(uint32_t base, uint32_t reg_offset) {
    return *(volatile uint32_t *)(base + reg_offset);
}

#endif /* I2C_MASTER_REGS_H */