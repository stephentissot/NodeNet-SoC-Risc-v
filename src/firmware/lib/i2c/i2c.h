#ifndef ZIPCPU_I2C_H
#define ZIPCPU_I2C_H

#include <cstdint>
#include "i2c_regs.h"

/*This I2C API is loosely based on Arduino's Wire API.*/

class ZipCpuI2C {
  private:
    uint32_t slaveAddr_; //7 bit slave address.
    uint32_t bufStartIdx_; //Starting index/address of an I2C read or write transaction.
    uint32_t readIdx_; //Keeps track of current read position of read() method.
    uint32_t numBytes_; //Number of bytes to send or receive over I2C.
    volatile uint8_t *i2c_mem_; //Pointer to the 256 byte memory buffer in the I2C master core. Holds the data to send / received.
    uint32_t base_;

    bool isBusy_();
    bool i2cError_();

  public:
    explicit ZipCpuI2C(uint32_t base = I2C_MASTER_BASE);

    void begin();
    void enableIRQ(bool enable);
    void ackIRQ();
    void setClock(uint32_t clockSpeedHz);
    void beginTransmission(uint8_t slaveAddr);
    uint8_t endTransmission();
    uint8_t requestFrom(uint8_t slaveAddr, uint8_t numBytes);
    uint8_t read(void);
    uint8_t write(uint8_t b);
};

extern ZipCpuI2C i2c;

#endif /*I2C_H*/
