#include "i2c.h"
#include "i2c_regs.h"
#include "bigsister.h"
#include <cassert>

#define SLAVE_ADDR_UNASSIGNED (~0UL)
#define BUF_IDX_UNKNOWN (~0UL)

//Default: 125*4 25MHz sysclock ticks per I2C clock -> 100kHz.
#define I2C_SPEED_DEFAULT 125u

//Singleton
ZipCpuI2C i2c;

ZipCpuI2C::ZipCpuI2C(uint32_t base)
    : i2c_mem_(reinterpret_cast<volatile uint8_t*>(base + I2C_MASTER_MEM_BASE)),
      base_(base),
      slaveAddr_(SLAVE_ADDR_UNASSIGNED),
      bufStartIdx_(BUF_IDX_UNKNOWN),
      readIdx_(BUF_IDX_UNKNOWN),
      numBytes_(0) {}

bool ZipCpuI2C::isBusy_() {
  return (i2c_master_reg_rd(base_, I2C_MASTER_CMD) & I2C_MASTER_CMD_BUSY) != 0;
}

bool ZipCpuI2C::i2cError_() {
  return (i2c_master_reg_rd(base_, I2C_MASTER_CMD) & I2C_MASTER_CMD_ERR) != 0;
}

static bool wait_for_idle(uint32_t base, uint32_t timeout_ms) {
  const uint32_t start = millis();
  while (true) {
    const uint32_t cmd = i2c_master_reg_rd(base, I2C_MASTER_CMD);
    if ((cmd & I2C_MASTER_CMD_BUSY) == 0u) {
      return true;
    }
    if ((millis() - start) >= timeout_ms) {
      return false;
    }
    compiler_barrier();
  }
}

void ZipCpuI2C::begin() {
  i2c_master_reg_wr(base_, I2C_MASTER_SPD, I2C_SPEED_DEFAULT);
  slaveAddr_ = SLAVE_ADDR_UNASSIGNED;
  bufStartIdx_ = BUF_IDX_UNKNOWN;
  readIdx_ = BUF_IDX_UNKNOWN;
  numBytes_ = 0;
}

void ZipCpuI2C::enableIRQ(bool enable) {
  i2c_master_reg_wr(base_, I2C_IEN, enable ? I2C_IEN_BUSY : 0u);
}

void ZipCpuI2C::ackIRQ() {
  i2c_master_reg_wr(base_, I2C_ISR, I2C_ISR_BUSY);
}

void ZipCpuI2C::setClock(uint32_t clockSpeedHz) {
  const uint32_t ticks_per_i2c_bit = BIGSISTER_CPU_HZ / (4u * clockSpeedHz);

  assert(ticks_per_i2c_bit <= I2C_MASTER_SPD_MASK);
  i2c_master_reg_wr(base_, I2C_MASTER_SPD, ticks_per_i2c_bit);
}

void ZipCpuI2C::beginTransmission(uint8_t slaveAddr) {
  slaveAddr_ = static_cast<uint32_t>(slaveAddr);
  bufStartIdx_ = 0;
  numBytes_ = 0;
  readIdx_ = 0;
}

uint8_t ZipCpuI2C::endTransmission() {
  uint8_t res = 0;

  if (slaveAddr_ == SLAVE_ADDR_UNASSIGNED) {
    return 1u;
  }

  if (numBytes_ > 0) {
    uint32_t cmdReg = numBytes_ & I2C_MASTER_CMD_NUM_BYTES_MASK;
    cmdReg |= (slaveAddr_ << I2C_MASTER_CMD_SLV_ADDR_OFFSET) & I2C_MASTER_CMD_SLV_ADDR_MASK;
    cmdReg |= (bufStartIdx_ << I2C_MASTER_CMD_START_ADDR_OFFSET) & I2C_MASTER_CMD_START_ADDR_MASK;
    cmdReg |= I2C_MASTER_CMD_WR;

    compiler_barrier();
    i2c_master_reg_wr(base_, I2C_MASTER_CMD, cmdReg);
    compiler_barrier();

    if (!wait_for_idle(base_, 20u)) {
      res = 1u;
    } else {
      res = i2cError_() ? 1u : 0u;
    }

    slaveAddr_ = SLAVE_ADDR_UNASSIGNED;
    readIdx_ = 0;
    bufStartIdx_ = 0;
    numBytes_ = 0;
  }

  return res;
}

uint8_t ZipCpuI2C::requestFrom(uint8_t slaveAddr, uint8_t numBytes) {
  if (slaveAddr != static_cast<uint8_t>(slaveAddr_) || numBytes == 0u) {
    return 1u;
  }

  numBytes_ = numBytes;

  uint32_t cmdReg = numBytes_ & I2C_MASTER_CMD_NUM_BYTES_MASK;
  cmdReg |= (slaveAddr_ << I2C_MASTER_CMD_SLV_ADDR_OFFSET) & I2C_MASTER_CMD_SLV_ADDR_MASK;
  cmdReg |= (bufStartIdx_ << I2C_MASTER_CMD_START_ADDR_OFFSET) & I2C_MASTER_CMD_START_ADDR_MASK;
  cmdReg |= I2C_MASTER_CMD_RD;

  compiler_barrier();
  i2c_master_reg_wr(base_, I2C_MASTER_CMD, cmdReg);
  compiler_barrier();

  if (!wait_for_idle(base_, 20u)) {
    return 1u;
  }

  readIdx_ = bufStartIdx_;
  slaveAddr_ = SLAVE_ADDR_UNASSIGNED;

  return i2cError_() ? 1u : 0u;
}

uint8_t ZipCpuI2C::read(void) {
  if (numBytes_ == 0u) {
    return 0u;
  }

  if (readIdx_ == numBytes_ + bufStartIdx_) {
    readIdx_ = bufStartIdx_;
  }

  return i2c_mem_[(readIdx_++) & 0xff];
}

uint8_t ZipCpuI2C::write(uint8_t b) {
  if (slaveAddr_ == SLAVE_ADDR_UNASSIGNED) {
    return 0u;
  }

  if (bufStartIdx_ == BUF_IDX_UNKNOWN) {
    bufStartIdx_ = 0;
  }

  if ((bufStartIdx_ + numBytes_) < I2C_MASTER_MEM_SIZE_BYTES) {
    i2c_mem_[(bufStartIdx_ + numBytes_) & 0xff] = b;
    ++numBytes_;
    return 1u;
  }

  return 0u;
}

