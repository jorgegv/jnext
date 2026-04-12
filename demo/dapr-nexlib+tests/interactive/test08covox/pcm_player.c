#include "pcm_player.h"
#include "dma.h"
#include <arch/zxn.h>

typedef struct
{
    u8 page;
    bool loop;
    void *start;
    void *end;
    u8 scaler;
} sample_table_t;

extern u8 jump_start[];
extern u8 jump_end[];
extern u8 death_start[];
extern u8 death_end[];

#define SAMPLE_SCALER 12

static sample_table_t sample_table[] =
{
    // page, loop, start, end
    { 94, false, jump_start,       jump_end,       104 },
    { 94, false, death_start,      death_end,      104 },
    // { 92, false, dub1_start,       dub1_end,       SAMPLE_SCALER * 12 },
    { 0,  false, 0xffff,           0xffff }
};

#include "print_tile.h"

void pcm_play(u8 sample_index)
{
    // using slot 2, mapped by default to 8k bank 10
    ZXN_NEXTREGA(0x52, sample_table[sample_index].page);
    u8 *sample_source = (void*)((0x4000) + (u16)(sample_table[sample_index].start));

    // using slot 3, mapped by default to 8k bank 10
    // ZXN_NEXTREGA(0x53, sample_table[sample_index].page);
    // u8 *sample_source = (void*)((0x6000) + (u16)(sample_table[sample_index].start));

    u16 sample_length = (u16)sample_table[sample_index].end - (u16)sample_table[sample_index].start;
    u8 sample_scaler = sample_table[sample_index].scaler;   
    
    dma_transfer_sample((void *)sample_source, sample_length, sample_scaler, sample_table[sample_index].loop);
}
