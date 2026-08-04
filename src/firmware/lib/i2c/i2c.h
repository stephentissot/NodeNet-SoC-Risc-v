#ifndef I2C_H
#define I2C_H

#include <stdint.h>
#include <stdbool.h>

/* API Publique en C classique (Sémantique Wire) */
void i2c_begin(void);
void i2c_setClock(uint32_t clockSpeedHz);
void i2c_beginTransmission(uint8_t slaveAddr);
void i2c_write(uint8_t b);
int  i2c_endTransmission(void);
int  i2c_requestFrom(uint8_t slaveAddr, uint8_t numBytes);
uint8_t i2c_read(void);

#endif /* I2C_H */
