#include "i2c.h"
#include "i2c_regs.h"
#include "bigsister.h"

/* Fonction de gestion du temps fournie par votre module wb_timer */
extern uint32_t millis(void);

#define SLAVE_ADDR_UNASSIGNED (~0UL)
#define BUF_IDX_UNKNOWN       (~0UL)

#define PICORV32_CPU_HZ         25000000u
#define I2C_SPEED_DEFAULT_HZ    100000u
#define I2C_SPEED_MIN_TICKS     1u
#define I2C_SPEED_MAX_TICKS     0x000FFFFFu

/* Barrières mémoires matérielles RISC-V indispensables sous -Os */
#define compiler_barrier() __asm__ volatile("" ::: "memory")
#define nop_barrier()      __asm__ volatile("nop" ::: "memory")

/* Variables d'état statiques fixes (Évite d'utiliser la pile et sécurise l'ABI) */
static uint32_t s_slave_addr    = SLAVE_ADDR_UNASSIGNED;
static uint32_t s_buf_start_idx = BUF_IDX_UNKNOWN;
static uint32_t s_read_idx      = BUF_IDX_UNKNOWN;
static uint32_t s_num_bytes     = 0u;

/* ─── Primitives d'accès à la RAM Proxy (zone mémoire byte-addressée) ─── */
static inline uint8_t i2c_mem_read_byte(uint32_t idx) {
    volatile uint8_t *i2c_mem = (volatile uint8_t*)(I2C_MASTER_BASE + I2C_MASTER_MEM_BASE);
    return i2c_mem[idx];
}

static inline void i2c_mem_write_byte(uint32_t idx, uint8_t value) {
    volatile uint8_t *i2c_mem = (volatile uint8_t*)(I2C_MASTER_BASE + I2C_MASTER_MEM_BASE);
    i2c_mem[idx] = value;
    compiler_barrier();
}

static inline uint32_t i2c_speed_ticks_for_hz(uint32_t clockSpeedHz) {
    if (clockSpeedHz == 0u) {
        return I2C_SPEED_MIN_TICKS;
    }

    const uint32_t ticks = PICORV32_CPU_HZ / clockSpeedHz;
    if (ticks < I2C_SPEED_MIN_TICKS) {
        return I2C_SPEED_MIN_TICKS;
    }
    if (ticks > I2C_SPEED_MAX_TICKS) {
        return I2C_SPEED_MAX_TICKS;
    }
    return ticks;
}

/* ─── Machine d'attente active synchrone et temporelle (Strictement Polling) ─── */
static bool wait_for_busy_or_error(uint32_t timeout_ms) {
    /* Délai initial de synchronisation (200 NOPs) pour laisser le temps au FPGA
       de décoder l'écriture et de lever le bit BUSY matériel avant la première relecture. */
    for (volatile int i = 0; i < 200; ++i) {
        __asm__ volatile("nop");
    }

    const uint32_t start = millis();
    bool status_has_been_busy = false;

    while (true) {
        const uint32_t cmd = i2c_master_reg_rd(I2C_MASTER_BASE, I2C_MASTER_CMD);
        
        /* 1. Capture du démarrage de la machine d'état de l'IP */
        if ((cmd & I2C_MASTER_CMD_BUSY) != 0u) {
            status_has_been_busy = true;
        }
        
        /* 2. Interception d'une erreur immédiate (NACK ou conflit) */
        if ((cmd & I2C_MASTER_CMD_ERR) != 0u) {
            return false;
        }
        
        /* 3. Fin nominale : le bus est repassé au repos après avoir travaillé */
        if (status_has_been_busy && (cmd & I2C_MASTER_CMD_BUSY) == 0u) {
            return true;
        }

        /* 4. Protection Timeout strict basé sur millis() */
        if ((millis() - start) >= timeout_ms) {
            return false;
        }
        nop_barrier(); // Impêche GCC d'éliminer la relecture physique du FPGA
    }
}

/* ─── Fonctions Publiques de l'API ──────────────────────────────────────── */

void i2c_begin(void) {
    /* Initialisation nominale : Vitesse d'abord, CMD à 0 ensuite */
    i2c_master_reg_wr(I2C_MASTER_BASE, I2C_MASTER_SPD,
                      i2c_speed_ticks_for_hz(I2C_SPEED_DEFAULT_HZ));
    i2c_master_reg_wr(I2C_MASTER_BASE, I2C_MASTER_CMD, 0u);
    
    s_slave_addr    = SLAVE_ADDR_UNASSIGNED;
    s_buf_start_idx = BUF_IDX_UNKNOWN;
    s_read_idx      = BUF_IDX_UNKNOWN;
    s_num_bytes     = 0u;
    compiler_barrier();
}

void i2c_setClock(uint32_t clockSpeedHz) {
    /* Pour un CPU à 25 MHz, 100 kHz I2C correspond à ~250 cycles par bit. */
    const uint32_t final_speed = i2c_speed_ticks_for_hz(clockSpeedHz);
    i2c_master_reg_wr(I2C_MASTER_BASE, I2C_MASTER_SPD, final_speed);
}

void i2c_beginTransmission(uint8_t slaveAddr) {
    s_slave_addr    = (uint32_t)slaveAddr;
    s_buf_start_idx = 0u;
    s_num_bytes     = 0u;
    s_read_idx      = 0u;
    compiler_barrier();
}

void i2c_write(uint8_t b) {
    if (s_slave_addr == SLAVE_ADDR_UNASSIGNED) return;

    if (s_buf_start_idx == BUF_IDX_UNKNOWN) {
        s_buf_start_idx = 0u;
    }

    /* La RAM proxy officielle (MEM_ADDR_BITS = 7) alloue 112 octets (soit 28 mots) */
    if ((s_buf_start_idx + s_num_bytes) < I2C_MASTER_MEM_SIZE_BYTES) {
        const uint32_t idx = s_buf_start_idx + s_num_bytes;
        i2c_mem_write_byte(idx, b);
        s_num_bytes++;
    }
}

int i2c_endTransmission(void) {
    if (s_slave_addr == SLAVE_ADDR_UNASSIGNED) return 1;
    int res = 0;

    if (s_num_bytes > 0u) {
        /* Construction du mot de commande officiel ZipCPU (START_ADDR décalé de 9) */
        uint32_t cmdReg = (s_num_bytes << I2C_MASTER_CMD_NUM_BYTES_OFFSET) & I2C_MASTER_CMD_NUM_BYTES_MASK;
        cmdReg |= (s_buf_start_idx << I2C_MASTER_CMD_START_ADDR_OFFSET) & I2C_MASTER_CMD_START_ADDR_MASK;
        cmdReg |= (s_slave_addr << I2C_MASTER_CMD_SLV_ADDR_OFFSET) & I2C_MASTER_CMD_SLV_ADDR_MASK;
        cmdReg |= I2C_MASTER_CMD_WR; // Bit 16 forcé à 0 pour l'écriture

        /* Amorçage par front (Force Trigger) : passage à 0 puis envoi de la commande */
        i2c_master_reg_wr(I2C_MASTER_BASE, I2C_MASTER_CMD, 0u);
        compiler_barrier();
        
        i2c_master_reg_wr(I2C_MASTER_BASE, I2C_MASTER_CMD, cmdReg);
        compiler_barrier();

        if (!wait_for_busy_or_error(20u)) {
            res = 2; // Timeout ou abandon matériel
        } else {
            /* Relecture finale du registre pour vérifier le bit ERR (30) */
            uint32_t final_cmd = i2c_master_reg_rd(I2C_MASTER_BASE, I2C_MASTER_CMD);
            res = (final_cmd & I2C_MASTER_CMD_ERR) ? 3 : 0;
        }

        s_slave_addr = SLAVE_ADDR_UNASSIGNED;
        s_num_bytes  = 0u;
    }

    return res;
}

int i2c_requestFrom(uint8_t slaveAddr, uint8_t numBytes) {
    s_slave_addr    = (uint32_t)slaveAddr;
    s_buf_start_idx = 0u;
    s_num_bytes     = (uint32_t)numBytes;

    uint32_t cmdReg = (s_num_bytes << I2C_MASTER_CMD_NUM_BYTES_OFFSET) & I2C_MASTER_CMD_NUM_BYTES_MASK;
    cmdReg |= (s_buf_start_idx << I2C_MASTER_CMD_START_ADDR_OFFSET) & I2C_MASTER_CMD_START_ADDR_MASK;
    cmdReg |= (s_slave_addr << I2C_MASTER_CMD_SLV_ADDR_OFFSET) & I2C_MASTER_CMD_SLV_ADDR_MASK;
    cmdReg |= I2C_MASTER_CMD_RD; // Bit 16 forcé à 1 pour déclencher la lecture

    i2c_master_reg_wr(I2C_MASTER_BASE, I2C_MASTER_CMD, 0u);
    compiler_barrier();
    
    i2c_master_reg_wr(I2C_MASTER_BASE, I2C_MASTER_CMD, cmdReg);
    compiler_barrier();

    if (!wait_for_busy_or_error(20u)) {
        return 2;
    }

    s_read_idx   = s_buf_start_idx;
    s_slave_addr = SLAVE_ADDR_UNASSIGNED;

    uint32_t final_cmd = i2c_master_reg_rd(I2C_MASTER_BASE, I2C_MASTER_CMD);
    return (final_cmd & I2C_MASTER_CMD_ERR) ? 3 : 0;
}

uint8_t i2c_read(void) {
    if (s_num_bytes == 0u) return 0u;

    if (s_read_idx == s_num_bytes + s_buf_start_idx) {
        s_read_idx = s_buf_start_idx;
    }

    const uint32_t idx = (s_read_idx++) % I2C_MASTER_MEM_SIZE_BYTES;
    return i2c_mem_read_byte(idx);
}

