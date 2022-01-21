#ifndef CHECKSUM_H
#define CHECKSUM_H

#include <stdint.h>

uint16_t crc16_table(uint8_t* btData, uint32_t wLen, uint16_t prev);

#endif // CHECKSUM_H
