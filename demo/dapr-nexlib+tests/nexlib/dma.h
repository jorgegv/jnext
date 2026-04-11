#ifndef _DMA_H
#define _DMA_H

#include <stdint.h>
#include <stdbool.h>

#define IO_DMA_PORT             0x6B
#define IO_SPRITE_PATTERN_DEST  0x005B
#define SAMPLE_COVOXPORT        0xffdf
#define SAMPLE_LOOP             D_WR5 | D_WR5_RESTART | D_WR5_CE_WAIT
#define SAMPLE_NOLOOP           D_WR5
#define SAMPLE_SCALER           12

void dma_transfer(void *dest, void *source, uint16_t length);
void dma_transfer_reverse(void *dest, void *source, uint16_t length);
void dma_transfer_port(void *source, uint16_t length);
void dma_transfer_sprite(void *source, uint16_t length);
void dma_transfer_sample(void *source, uint16_t length, uint8_t scaler, bool loop);
void dma_fill(void *dest, uint8_t fill_value, uint16_t length);

#endif
