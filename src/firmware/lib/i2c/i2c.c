#include "i2c.h"
#include "i2c_regs.h"
#include "bigsister.h"

#include <stdbool.h>

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
    const uint32_t lane = idx & 0x3u;
    volatile uint32_t *i2c_mem_words = (volatile uint32_t *)(I2C_MASTER_BASE + I2C_MASTER_MEM_BASE + (idx & ~0x3u));
    const uint32_t word = *i2c_mem_words;
    return (uint8_t)((word >> (lane * 8u)) & 0xFFu);
}

static inline void i2c_mem_write_byte(uint32_t idx, uint8_t value) {
    const uint32_t lane = idx & 0x3u;
    volatile uint32_t *i2c_mem_words = (volatile uint32_t *)(I2C_MASTER_BASE + I2C_MASTER_MEM_BASE + (idx & ~0x3u));
    uint32_t word = *i2c_mem_words;
    word &= ~(0xFFu << (lane * 8u));
    word |= ((uint32_t)value << (lane * 8u));
    *i2c_mem_words = word;
    compiler_barrier();
}

static inline void i2c_mem_clear_range(uint32_t start_idx, uint32_t count) {
    for (uint32_t i = 0u; i < count; ++i) {
        i2c_mem_write_byte(start_idx + i, 0u);
    }
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

/* ─── Attente de fin de transaction ─── */
static bool wait_for_busy_or_error(uint32_t timeout_ms) {
    const uint32_t start = millis();
    bool saw_busy = false;

    while (true) {
        const uint32_t cmd = i2c_master_reg_rd(I2C_MASTER_BASE, I2C_MASTER_CMD);

        if ((cmd & I2C_MASTER_CMD_BUSY) != 0u) {
            saw_busy = true;
        }

        if ((cmd & I2C_MASTER_CMD_ERR) != 0u) {
            return false;
        }

        if (saw_busy && (cmd & I2C_MASTER_CMD_BUSY) == 0u) {
            return true;
        }

        if ((millis() - start) >= timeout_ms) {
            return false;
        }

        nop_barrier();
    }
}

/* ─── Fonctions Publiques de l'API ──────────────────────────────────────── */

void i2c_begin(void) {
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
    if (s_slave_addr == SLAVE_ADDR_UNASSIGNED) {
        return;
    }

    if (s_buf_start_idx == BUF_IDX_UNKNOWN) {
        s_buf_start_idx = 0u;
    }

    if ((s_buf_start_idx + s_num_bytes) < I2C_MASTER_MEM_SIZE_BYTES) {
        const uint32_t idx = s_buf_start_idx + s_num_bytes;
        i2c_mem_write_byte(idx, b);
        s_num_bytes++;
    }
}

int i2c_endTransmission(void) {
    if (s_slave_addr == SLAVE_ADDR_UNASSIGNED) {
        return 1;
    }

    if (s_num_bytes == 0u) {
        return 0;
    }

    uint32_t cmdReg = (s_num_bytes << I2C_MASTER_CMD_NUM_BYTES_OFFSET) & I2C_MASTER_CMD_NUM_BYTES_MASK;
    cmdReg |= (s_buf_start_idx << I2C_MASTER_CMD_START_ADDR_OFFSET) & I2C_MASTER_CMD_START_ADDR_MASK;
    cmdReg |= (s_slave_addr << I2C_MASTER_CMD_SLV_ADDR_OFFSET) & I2C_MASTER_CMD_SLV_ADDR_MASK;
    cmdReg |= I2C_MASTER_CMD_WR;

    i2c_master_reg_wr(I2C_MASTER_BASE, I2C_MASTER_CMD, 0u);
    compiler_barrier();
    i2c_master_reg_wr(I2C_MASTER_BASE, I2C_MASTER_CMD, cmdReg);
    compiler_barrier();

    const bool ok = wait_for_busy_or_error(20u);
    const uint32_t final_cmd = i2c_master_reg_rd(I2C_MASTER_BASE, I2C_MASTER_CMD);
    const int res = (!ok || (final_cmd & I2C_MASTER_CMD_ERR)) ? 2 : 0;

    if (res == 0) {
        i2c_mem_clear_range(s_buf_start_idx, s_num_bytes);
    }

    s_slave_addr = SLAVE_ADDR_UNASSIGNED;
    s_num_bytes  = 0u;
    s_read_idx   = 0u;
    return res;
}

int i2c_requestFrom(uint8_t slaveAddr, uint8_t numBytes) {
    if (numBytes == 0u) {
        return 0;
    }

    s_slave_addr    = (uint32_t)slaveAddr;
    s_buf_start_idx = 0u;
    s_num_bytes     = (uint32_t)numBytes;
    s_read_idx      = 0u;

    uint32_t cmdReg = (s_num_bytes << I2C_MASTER_CMD_NUM_BYTES_OFFSET) & I2C_MASTER_CMD_NUM_BYTES_MASK;
    cmdReg |= (s_buf_start_idx << I2C_MASTER_CMD_START_ADDR_OFFSET) & I2C_MASTER_CMD_START_ADDR_MASK;
    cmdReg |= (s_slave_addr << I2C_MASTER_CMD_SLV_ADDR_OFFSET) & I2C_MASTER_CMD_SLV_ADDR_MASK;
    cmdReg |= I2C_MASTER_CMD_RD;

    i2c_master_reg_wr(I2C_MASTER_BASE, I2C_MASTER_CMD, 0u);
    compiler_barrier();
    i2c_master_reg_wr(I2C_MASTER_BASE, I2C_MASTER_CMD, cmdReg);
    compiler_barrier();

    const bool ok = wait_for_busy_or_error(20u);
    s_read_idx = s_buf_start_idx;
    s_slave_addr = SLAVE_ADDR_UNASSIGNED;

    const uint32_t final_cmd = i2c_master_reg_rd(I2C_MASTER_BASE, I2C_MASTER_CMD);
    return (!ok || (final_cmd & I2C_MASTER_CMD_ERR)) ? 2 : 0;
}

uint8_t i2c_read(void) {
    if (s_num_bytes == 0u) {
        return 0u;
    }

    if (s_read_idx == s_num_bytes + s_buf_start_idx) {
        s_read_idx = s_buf_start_idx;
    }

    const uint32_t idx = (s_read_idx++) % I2C_MASTER_MEM_SIZE_BYTES;
    return i2c_mem_read_byte(idx);
}

