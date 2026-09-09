#ifndef W25Q_SPI_H
#define W25Q_SPI_H

#include <stdint.h>

void     W25Q_SPI_Init(void);
void     W25Q_CS_Low(void);
void     W25Q_CS_High(void);
void     W25Q_SPI_SendByte(uint8_t tx);
uint8_t  W25Q_SPI_RecvByte(void);
uint8_t  W25Q_SPI_TransferByte(uint8_t tx);
void     W25Q_SPI_Transmit(const uint8_t *tx, uint32_t len);
void     W25Q_SPI_Receive(uint8_t *rx, uint32_t len);

#endif /* W25Q_SPI_H */
