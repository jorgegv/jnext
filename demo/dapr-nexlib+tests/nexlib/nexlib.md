# NEXLIB - ZX Spectrum Next C Library

**AI description created by Claude Sonnet 4.5**

## Introduction

NEXLIB is a C library designed specifically for the ZX Spectrum Next platform, providing developers with convenient access to the enhanced hardware features of the Next. The library abstracts complex hardware interactions into simple C function calls, making game and application development more accessible while maintaining high performance through optimized assembly implementations.

### Key Features

The library provides comprehensive support for:

- **Graphics Layers**: Layer 2 (bitmap graphics), Tilemap (character/tile-based display), Sprites (hardware-accelerated)
- **Input Handling**: Keyboard (Spectrum and Next extended keys), Joystick (Kempston/Sinclair compatible)
- **Display Management**: Clipping windows, palette control, screen context switching
- **Memory Management**: Bank paging for accessing the Next's expanded memory
- **DMA Operations**: Fast memory transfers using the Next's DMA controller
- **Interrupt Handling**: IM2 interrupt system with custom callback support
- **Utilities**: Mathematical functions, string operations, ZX0 decompression, text printing

The library is optimized for the Z80N processor and uses both C and assembly implementations. Many functions use the `fastcall` calling convention (single parameter passed in registers) for maximum efficiency on the Z80 architecture.

---

## Function Reference

### Graphics - Layer 2

Layer 2 provides a 256x192 or 320x256 pixel bitmap display with hardware scrolling capabilities.

#### `void layer2_init(void)`
Initialize Layer 2 with default settings:
- Sets global transparency to color 0
- Sets resolution to 320x256x8 mode
- Sets RAM page to 9
- Makes Layer 2 visible

#### `void layer2_set_resolution(u8 res) fastcall`
Set the Layer 2 display resolution.
- `res`: Resolution mode constant
  - `L2RES_256x192x8` (0x00) - 256×192 pixels, 8-bit color
  - `L2RES_320x256x8` (0x10) - 320×256 pixels, 8-bit color
  - `L2RES_640x256x4` (0x20) - 640×256 pixels, 4-bit color

#### `void layer2_set_visible(void)`
Make Layer 2 visible on screen.

#### `void layer2_set_invisible(void)`
Hide Layer 2 from screen.

#### `void layer2_set_ram_page(u8 num_16k_bank) fastcall`
Set the starting 16K bank for Layer 2 display data.
- `num_16k_bank`: Bank number (typically 9 for default Layer 2 area)

#### `void layer2_set_first_palette_entry(u16 palentry) fastcall`
Set the first palette entry for Layer 2.
- `palentry`: 9-bit RGB333 palette value (format: MSB RRRGGGBB 0000000B LSB)

#### `void layer2_clear_pages(void)`
Clear all Layer 2 display pages to transparent color.

#### `void layer2_set_hoffset(u16 hoff) fastcall`
Set horizontal scrolling offset for Layer 2.
- `hoff`: Horizontal offset in pixels (0-319 for 320-pixel mode)

---

### Graphics - Tilemap

The tilemap layer provides hardware-accelerated character and tile-based graphics.

#### `void tilemap_setup(void)`
Initialize the tilemap system with default settings.

#### `void tilemap_disable(void)`
Disable tilemap display.

#### `void tilemap_set_40col(void)`
Configure tilemap for 40-column mode (8×8 pixel tiles).

#### `void tilemap_set_80col(void)`
Configure tilemap for 80-column mode (4×8 pixel tiles).

#### `void tilemap_set_tilemap_seg(u8 tmseg) fastcall`
Set the memory bank segment for tilemap data.
- `tmseg`: Segment number for tilemap storage

#### `void tilemap_set_tileset_seg(u8 tsseg) fastcall`
Set the memory bank segment for tileset definitions.
- `tsseg`: Segment number for tileset storage

#### `void tilemap_clear_buf_value(u16 value) fastcall`
Clear tilemap buffer with specified tile value.
- `value`: 16-bit value (tile index + attributes) to fill

#### `void tilemap_set_hoffset(s16 hoffset) fastcall`
Set horizontal scrolling offset for tilemap.
- `hoffset`: Horizontal offset in pixels

#### `void tilemap_set_voffset(s8 voffset) fastcall`
Set vertical scrolling offset for tilemap.
- `voffset`: Vertical offset in pixels

---

### Graphics - Sprites

Hardware sprite system supporting up to 128 sprites with various sizes and transformations.

#### `void sprite_setup(void)`
Initialize the sprite system.

#### `void sprite_load_defs(void)`
Load sprite pattern definitions into sprite memory.

#### `void sprite_clear_all_slots(void)`
Clear all sprite slots, making all sprites invisible.

#### `void sprite_clear_all_from(u8 first_slot_to_clear)`
Clear sprite slots starting from a specific slot.
- `first_slot_to_clear`: First slot number to clear (0-127)

#### `void sprite_clear_slot(u8 slot)`
Clear a specific sprite slot.
- `slot`: Sprite slot number (0-127)

#### `typedef struct SpriteDef`
Sprite definition structure:
```c
typedef struct {
    u8  slot;       // sprite slot number (0-127)
    s16 x;          // x coordinate
    s16 y;          // y coordinate
    u8  pal;        // palette offset
    u8  mirrot;     // mirror X/Y and rotation flags
    u8  pat;        // pattern index
    u8  scale;      // scale factor
} SpriteDef;
```

#### `void sprite_update_c(SpriteDef* spr)`
Update a sprite's hardware attributes (C implementation).
- `spr`: Pointer to SpriteDef structure

#### `void sprite_update_asm(SpriteDef* spr)`
Update a sprite's hardware attributes (optimized assembly implementation).
- `spr`: Pointer to SpriteDef structure

#### `void sprite_init_quad(SpriteDef* sprdef)`
Initialize a quad sprite (4 hardware sprites working together).
- `sprdef`: Pointer to SpriteDef for the first sprite of the quad

#### `void sprite_update_quad(SpriteDef* spr)`
Update a quad sprite's position and attributes.
- `spr`: Pointer to SpriteDef for the first sprite of the quad

#### `void sprite_init_hpair(SpriteDef* sprdef)`
Initialize a horizontal pair of sprites.
- `sprdef`: Pointer to SpriteDef for the first sprite

#### `void sprite_update_hpair(SpriteDef* spr)`
Update a horizontal sprite pair.
- `spr`: Pointer to SpriteDef for the first sprite

#### `void sprite_update_pos(SpriteDef* sprdef)`
Update only the position of a sprite (faster than full update).
- `sprdef`: Pointer to SpriteDef structure

#### `void sprite_text_print(SpriteDef* sdef, const char* msg, bool centerX)`
Print text using sprites.
- `sdef`: Pointer to SpriteDef for first character sprite
- `msg`: Text message to display
- `centerX`: If true, center the text horizontally

**Scale Constants**:
- `SCALE_Y1` (0x00) - Normal Y scale
- `SCALE_Y2` (0x02) - 2× Y scale
- `SCALE_Y4` (0x04) - 4× Y scale
- `SCALE_Y8` (0x06) - 8× Y scale
- `SCALE_X1` (0x00) - Normal X scale
- `SCALE_X2` (0x08) - 2× X scale
- `SCALE_X4` (0x10) - 4× X scale
- `SCALE_X8` (0x18) - 8× X scale

---

### Graphics - Palette Management

Control hardware palettes for different display layers.

#### `void palette_load_tilemap(u8* pal_data)`
Load 256-color palette for tilemap (first palette).
- `pal_data`: Pointer to 512 bytes of palette data (9-bit RGB333 format)

#### `void palette_load_tilemap_second(u8* pal_data)`
Load second tilemap palette.
- `pal_data`: Pointer to palette data

#### `void palette_load_sprites(u8* pal_data)`
Load sprite palette (256 colors).
- `pal_data`: Pointer to palette data

#### `void palette_load_layer2(u8* pal_data)`
Load Layer 2 palette (256 colors).
- `pal_data`: Pointer to palette data

#### `void palette_load_layer2_16col(u8* pal_data)`
Load first 16 colors of Layer 2 palette.
- `pal_data`: Pointer to 32 bytes of palette data

---

### Graphics - Clipping

Hardware clipping windows for restricting display areas.

#### `void clip_ctr_all(u16 width, u16 height)`
Set centered clipping window for all layers (Layer 2, tilemap, sprites).
- `width`: Width of visible area (pixels)
- `height`: Height of visible area (pixels)

The clipping is automatically centered within the maximum 320×256 viewing zone.

#### `void clip_ctr_layer2(u16 width, u16 height)`
Set centered clipping window for Layer 2 only.
- `width`: Width of clipping window
- `height`: Height of clipping window

#### `void clip_ctr_sprites(u16 width, u16 height)`
Set centered clipping window for sprites only.
- `width`: Width of clipping window
- `height`: Height of clipping window

#### `void clip_ctr_tilemap(u16 width, u16 height)`
Set centered clipping window for tilemap only.
- `width`: Width of clipping window
- `height`: Height of clipping window

#### `void clip_ula(u8 x1, u8 x2, u8 y1, u8 y2)`
Set clipping window for ULA layer.
- `x1`, `y1`: Top-left corner coordinates
- `x2`, `y2`: Bottom-right corner coordinates

---

### Input - Keyboard

Keyboard input handling with support for standard Spectrum and Next extended keys.

#### `void keyb_init(void)`
Initialize keyboard system. Must be called before using keyboard functions.

#### `void keyb_update(void)`
Update keyboard state. Call once per frame before checking key states.

#### `bool keyb_is_pressed(u16 keycode) fastcall`
Check if a key is currently pressed.
- `keycode`: Key code constant (e.g., `KEY_ZX_Q`, `KEY_ZX_SPACE`)
- Returns: `true` if pressed, `false` otherwise

#### `bool keyb_was_pressed(u16 keycode) fastcall`
Check if a key was pressed in the previous frame.
- `keycode`: Key code constant
- Returns: `true` if pressed last frame, `false` otherwise

#### `bool keyb_is_just_pressed(u16 keycode) fastcall`
Check if a key was just pressed (edge detection - pressed now but not last frame).
- `keycode`: Key code constant
- Returns: `true` if just pressed, `false` otherwise

#### `bool keyb_is_just_released(u16 keycode) fastcall`
Check if a key was just released (edge detection).
- `keycode`: Key code constant
- Returns: `true` if just released, `false` otherwise

#### `bool keyb_is_pressed_any(void)`
Check if any key is currently pressed.
- Returns: `true` if any key is pressed

#### `bool keyb_was_pressed_any(void)`
Check if any key was pressed in the previous frame.
- Returns: `true` if any key was pressed last frame

#### `bool keyb_is_just_pressed_any(void)`
Check if any key was just pressed.
- Returns: `true` if any key just pressed

#### `bool keyb_is_just_released_any(void)`
Check if any key was just released.
- Returns: `true` if any key just released

#### `u8 keyb_count(void)`
Get number of keys currently pressed.
- Returns: Count of pressed keys

#### `u16 keyb_code(void)`
Get the key code of a pressed key. If multiple keys are pressed, result is undefined.
- Returns: Key code, or 0 if no key pressed

#### `u8 keyb_short_for_code(u16 code) fastcall`
Get compact representation of key code.
- `code`: Key code
- Returns: Short code value

#### `void keyb_print_debug(u8 x, u8 y)`
Print debug information about keyboard state.
- `x`, `y`: Position on screen

#### `void keyb_codes_debug(u8 x, u8 y)`
Print key codes for debugging.
- `x`, `y`: Position on screen

**Key Code Constants** (partial list):
- `KEY_ZX_1` through `KEY_ZX_5` - Number keys 1-5
- `KEY_ZX_6` through `KEY_ZX_0` - Number keys 6-0
- `KEY_ZX_Q` through `KEY_ZX_T` - Letter keys Q-T
- `KEY_ZX_P` through `KEY_ZX_Y` - Letter keys P-Y
- Additional letter keys defined in similar pattern

**Keyboard State Variables**:
```c
extern u8 zxkey54321;   // State of keys 5,4,3,2,1
extern u8 zxkey67890;   // State of keys 6,7,8,9,0
extern u8 zxkeyTREWQ;   // State of keys T,R,E,W,Q
extern u8 zxkeyYUIOP;   // State of keys Y,U,I,O,P
extern u8 zxkeyGFDSA;   // State of keys G,F,D,S,A
extern u8 zxkeyHJKLe;   // State of keys H,J,K,L,Enter
extern u8 zxkeyVCXZc;   // State of keys V,C,X,Z,Caps
extern u8 zxkeyBNMys;   // State of keys B,N,M,Symbol,Space
extern u8 nxkey0;       // Next extended keys group 0
extern u8 nxkey1;       // Next extended keys group 1
```

---

### Input - Joystick

Joystick input handling supporting Kempston and Sinclair interfaces.

#### `void joystick_init(void)`
Initialize joystick system.

#### `void joystick_update(void)`
Update joystick state. Call once per frame before checking joystick state.

#### `bool joystick_is_pressed(u8 joycode) fastcall`
Check if joystick direction or button is currently pressed.
- `joycode`: Joystick code constant (e.g., `JOY_UP`, `JOY_BUT1`)
- Returns: `true` if pressed

#### `bool joystick_was_pressed(u8 joycode) fastcall`
Check if joystick direction/button was pressed in previous frame.
- `joycode`: Joystick code constant
- Returns: `true` if was pressed

#### `bool joystick_is_just_pressed(u8 joycode) fastcall`
Check if joystick direction/button was just pressed (edge detection).
- `joycode`: Joystick code constant
- Returns: `true` if just pressed

#### `bool joystick_is_just_released(u8 joycode) fastcall`
Check if joystick direction/button was just released.
- `joycode`: Joystick code constant
- Returns: `true` if just released

#### `bool joystick_is_pressed_any(void)`
Check if any joystick direction or button is pressed.
- Returns: `true` if any input active

#### `bool joystick_was_pressed_any(void)`
Check if any joystick input was pressed in previous frame.
- Returns: `true` if any was pressed

#### `bool joystick_is_just_pressed_any(void)`
Check if any joystick input was just pressed.
- Returns: `true` if any just pressed

#### `bool joystick_is_just_released_any(void)`
Check if any joystick input was just released.
- Returns: `true` if any just released

#### `u8 joystick_count(void)`
Get number of joystick directions/buttons currently pressed.
- Returns: Count of active inputs

#### `u8 joystick_code(void)`
Get the code of pressed joystick input. If multiple inputs are active, result is undefined.
- Returns: Joystick code, or 0 if none pressed

#### `u8 joystick_short_for_code(u8 code) fastcall`
Get compact representation of joystick code.
- `code`: Joystick code
- Returns: Short code value

#### `void joystick_update_flags(void)`
Update individual flag variables for joystick state. After calling this, the `joyLeft`, `joyRight`, etc. variables are updated.

**Joystick Code Constants**:
- `JOY_UP` (0x08) - Up direction
- `JOY_DOWN` (0x04) - Down direction
- `JOY_LEFT` (0x02) - Left direction
- `JOY_RIGHT` (0x01) - Right direction
- `JOY_BUT1` (0x10) - Button 1
- `JOY_BUT2` (0x20) - Button 2
- `JOY_BUT3` (0x40) - Button 3
- `JOY_BUT4` (0x80) - Button 4

**Joystick State Variables**:
```c
extern u8 joydata;   // Current frame joystick data
extern u8 joyprev;   // Previous frame joystick data
extern u8 joyLeft;   // Left flag (after joystick_update_flags)
extern u8 joyRight;  // Right flag
extern u8 joyUp;     // Up flag
extern u8 joyDown;   // Down flag
extern u8 joyBut1;   // Button 1 flag
extern u8 joyBut2;   // Button 2 flag
extern u8 joyBut3;   // Button 3 flag
extern u8 joyBut4;   // Button 4 flag
```

---

### Memory Management - Paging

Bank paging functions for accessing the Next's expanded memory (2MB organized as 256 banks of 8KB each).

#### `void storePrevPagesAtSlots0and1(void)`
Save the current memory page configuration for slots 0 and 1 (addresses $0000-$3FFF).

#### `void restorePrevPagesAtSlots0and1(void)`
Restore previously saved page configuration for slots 0 and 1.

#### `void putPagesAtSlots0and1(u8 page)`
Map a memory page into slots 0 and 1.
- `page`: 8KB page number to map

#### `void putRomPagesAtSlots0and1(void)`
Map ROM into slots 0 and 1.

#### `void storePrevPagesAtSlots2and3(void)`
Save current memory page configuration for slots 2 and 3 (addresses $4000-$7FFF).

#### `void restorePrevPagesAtSlots2and3(void)`
Restore previously saved page configuration for slots 2 and 3.

#### `void putPagesAtSlots2and3(u8 page)`
Map a memory page into slots 2 and 3.
- `page`: 8KB page number to map

---

### Memory Management - DMA

DMA (Direct Memory Access) controller functions for fast memory transfers.

#### `void dma_transfer(void* dest, void* source, uint16_t length)`
Perform a DMA memory transfer.
- `dest`: Destination address
- `source`: Source address
- `length`: Number of bytes to transfer

#### `void dma_transfer_reverse(void* dest, void* source, uint16_t length)`
Perform a DMA transfer in reverse direction (useful for overlapping regions).
- `dest`: Destination address
- `source`: Source address
- `length`: Number of bytes to transfer

#### `void dma_transfer_port(void* source, uint16_t length)`
Transfer data to an I/O port using DMA.
- `source`: Source address
- `length`: Number of bytes to transfer

#### `void dma_transfer_sprite(void* source, uint16_t length)`
Optimized DMA transfer for sprite pattern data.
- `source`: Source address of sprite data
- `length`: Number of bytes to transfer

#### `void dma_transfer_sample(void* source, uint16_t length, uint8_t scaler, bool loop)`
Transfer audio sample data using DMA.
- `source`: Source address of sample data
- `length`: Sample length in bytes
- `scaler`: Sample rate scaler
- `loop`: If true, loop the sample

#### `void dma_fill(void* dest, uint8_t fill_value, uint16_t length)`
Fill memory region with a specific byte value using DMA.
- `dest`: Destination address
- `fill_value`: Byte value to fill
- `length`: Number of bytes to fill

**DMA Constants**:
- `IO_DMA_PORT` (0x6B) - DMA control port
- `IO_SPRITE_PATTERN_DEST` (0x005B) - Sprite pattern destination
- `SAMPLE_COVOXPORT` (0xffdf) - Covox audio port
- `SAMPLE_SCALER` (12) - Default sample scaler value

---

### Display - Interrupts

Interrupt handling system for frame-synchronized game logic.

#### `void interrupt_init_FE(void)`
Initialize IM2 interrupt system using the FE vector table. This places the interrupt vector at $FE00-$FF00, leaving $FE01-$FFFF for stack space (511 bytes).

#### `void interrupt_init_FD(void)`
Initialize IM2 interrupt system using the FD vector table. This places the interrupt vector at $FD00-$FE00, leaving $FE01-$FFFF for stack space (511 bytes).

#### `extern FunPtr on_interrupt_callback`
Function pointer for interrupt callback. Assign your interrupt handler to this variable. The handler will be called at every vertical blank (50Hz PAL or 60Hz NTSC).

Example:
```c
void my_interrupt_handler(void) {
    // Your code here
}

// Later in initialization:
on_interrupt_callback = my_interrupt_handler;
interrupt_init_FE();
```

#### `interrupt_setup_scanline_only(SCANLINE)` (macro)
Setup interrupt to trigger only at a specific scanline, disabling ULA interrupt.
- `SCANLINE`: Scanline number (0-255) at which to trigger

#### `interrupt_get_scanline()` (macro)
Get the currently configured scanline interrupt value.
- Returns: Scanline number

#### `extern void* interrupt_isr_vector`
Pointer to the interrupt service routine vector address. Useful for debugging.

---

### Display - Screen Context

Screen context management for switching between different display configurations.

#### `typedef struct ScrCtx`
Screen context structure containing all resources and configuration for a screen:
```c
typedef struct SCRCTX {
    BlockRes layer2_0A, layer2_0B;      // Layer 2 banks
    BlockRes layer2_1A, layer2_1B;
    BlockRes layer2_2A, layer2_2B;
    BlockRes layer2_3A, layer2_3B;
    BlockRes layer2_4A, layer2_4B;
    
    PalRes layer2_pal;                   // Layer 2 palette
    
    BlockRes tileset_main;               // Main tileset
    PalRes tileset_main_pal;             // Main tileset palette
    BlockRes tileset_main_palgrp;        // Palette group
    
    BlockRes tileset_text;               // Text tileset
    PalRes tileset_text_pal;             // Text palette
    
    BlockRes spriteset_A, spriteset_B;   // Sprite sets
    PalRes spriteset_pal;                // Sprite palette
    
    BlockRes tilemap_A, tilemap_B;       // Tilemaps
    BlockRes collmap_A, collmap_B;       // Collision maps
    
    BlockRes levdata;                    // Level data
} ScrCtx;
```

#### `typedef struct BlockRes`
Resource block definition:
```c
typedef struct BLOCKRES {
    u8 page;           // Memory page number
    u8* cmp_data;      // Pointer to compressed data
} BlockRes;
```

#### `typedef struct PalRes`
Palette resource definition:
```c
typedef struct PALRES {
    u8 page;           // Memory page number
    u8* cmp_data;      // Pointer to compressed data
    u8 count;          // Number of colors
    bool has_fade;     // Whether palette supports fading
} PalRes;
```

#### `void sc_load_context(ScrCtx* ctx)`
Load a screen context, decompressing and setting up all resources.
- `ctx`: Pointer to ScrCtx structure with configured resources

---

### Display - Screen Controller

High-level screen/scene management system.

#### `void sc_switch_delay(u8 nframes)`
Set a delay (in frames) before switching to the next screen.
- `nframes`: Number of frames to delay

#### `void sc_switch_screen(FunPtr entry, FunPtr update, FunPtr exit)`
Schedule a switch to a new screen.
- `entry`: Function to call when entering the screen (can be NULL)
- `update`: Function to call every frame while on the screen (can be NULL)
- `exit`: Function to call when exiting the screen (can be NULL)

#### `void sc_update(void)`
Update screen controller. Call once per frame. This handles:
- Calling the current screen's update function
- Processing screen switch delays
- Calling exit/entry functions when switching screens

---

### Utilities - Text Printing

Text output functions for printing to the tilemap.

#### `print_set_pos(x, y)` (macro)
Set cursor position for text printing.
- `x`: Column position (0-39 or 0-79 depending on mode)
- `y`: Row position (0-23)

#### `void print_set_attr(u8 attr) fastcall`
Set text attribute (color, mirror, rotation flags).
- `attr`: Attribute byte value

#### `void print_set_color(u8 col) fastcall`
Set text color/palette offset.
- `col`: Color value

#### `void print_set_symbol(u8 symbol) fastcall`
Set current symbol/tile index for drawing.
- `symbol`: Symbol index

#### `void print_symbol(void)`
Print the current symbol at the current cursor position.

#### `void print_char(char ch) fastcall`
Print a character at the current cursor position.
- `ch`: Character to print

#### `void print_str(const char* str) fastcall`
Print a null-terminated string at the current cursor position.
- `str`: String to print

#### `void print_hex_nibble(u8 val) fastcall`
Print a 4-bit value as hexadecimal (0-F).
- `val`: Value to print (only lower 4 bits used)

#### `void print_hex_byte(u8 val) fastcall`
Print an 8-bit value as hexadecimal (00-FF).
- `val`: Byte value to print

#### `void print_hex_word(u16 val) fastcall`
Print a 16-bit value as hexadecimal (0000-FFFF).
- `val`: Word value to print

#### `void print_dec_byte(u8 val)`
Print an 8-bit value as decimal (0-255).
- `val`: Byte value to print

#### `void print_dec_word(u16 val)`
Print a 16-bit value as decimal (0-65535).
- `val`: Word value to print

#### `void print_frame(u8 x, u8 y, u8 w, u8 h)`
Draw a frame/border at specified position. Symbol and color must be set first.
- `x`, `y`: Top-left corner position
- `w`, `h`: Width and height in tiles

#### `void print_rect_symbol(u8 x, u8 y, u8 w, u8 h)`
Fill a rectangle with the current symbol.
- `x`, `y`: Top-left corner position
- `w`, `h`: Width and height in tiles

#### `void print_frame_filled(u8 x, u8 y, u8 w, u8 h)`
Draw a filled frame/border.
- `x`, `y`: Top-left corner position
- `w`, `h`: Width and height in tiles

#### `void print_set_pos_inc(sbyte x, sbyte y)`
Increment cursor position by relative offset.
- `x`, `y`: Relative offset from current position

#### `void println(const char* txt)`
Print a line of text (moves to next line after printing).
- `txt`: String to print

#### `void println_ctr(const char* txt, u8 len)`
Print centered text.
- `txt`: String to print
- `len`: Length of string

#### `const char* str_dec_for_u16(u16 u16arg) fastcall`
Convert 16-bit unsigned integer to decimal string.
- `u16arg`: Value to convert
- Returns: Pointer to static string buffer

#### `const char* str_dec_for_u8(u8 u8arg)`
Convert 8-bit unsigned integer to decimal string.
- `u8arg`: Value to convert
- Returns: Pointer to static string buffer

**Text Attribute Constants**:
- `ATTR_____` (0) - No transformation
- `ATTR___R_` (2) - Rotate 90°
- `ATTR__Y__` (4) - Mirror Y
- `ATTR__YR_` (6) - Mirror Y + Rotate
- `ATTR_X___` (8) - Mirror X
- `ATTR_X_R_` (10) - Mirror X + Rotate
- `ATTR_XY__` (12) - Mirror X+Y
- `ATTR_XYR_` (14) - Mirror X+Y + Rotate

**Debug Print Macros**:
- `DBGTEXT(x,y,txt)` - Print debug text
- `DBG4X(x,y,v4)` - Print 4-bit hex value
- `DBG8X(x,y,v8)` - Print 8-bit hex value
- `DBG16X(x,y,v16)` - Print 16-bit hex value
- `DBG4(x,y,v4)` - Print 4-bit decimal value
- `DBG8(x,y,v8)` - Print 8-bit decimal value
- `DBG16(x,y,v16)` - Print 16-bit decimal value

---

### Utilities - String Operations

String manipulation and formatting functions.

#### `const char* str_hex_for_u8(u8 u8val) fastcall`
Convert 8-bit value to hexadecimal string (2 characters + null).
- `u8val`: Value to convert
- Returns: Pointer to static string buffer

#### `const char* str_hex_for_u16(u16 u16val) fastcall`
Convert 16-bit value to hexadecimal string (4 characters + null).
- `u16val`: Value to convert
- Returns: Pointer to static string buffer

#### `const char* str_bin_for_u8(u8 u8val) fastcall`
Convert 8-bit value to binary string (8 characters + null).
- `u8val`: Value to convert
- Returns: Pointer to static string buffer

#### `const char* str_bin_for_u16(u16 u16val) fastcall`
Convert 16-bit value to binary string (16 characters + null).
- `u16val`: Value to convert
- Returns: Pointer to static string buffer

#### `char chr_hex_for_u4(u8 u4val) fastcall`
Convert 4-bit value to single hexadecimal character ('0'-'9', 'A'-'F').
- `u4val`: Value to convert (0-15)
- Returns: Hexadecimal character

---

### Utilities - Mathematical Functions

Optimized math operations for the Z80 processor.

#### `u16 sign16(s16 val) fastcall`
Get sign of 16-bit signed integer.
- `val`: Input value
- Returns: -1 if negative, 0 if zero, 1 if positive

#### `s8 sign8(s8 val) fastcall`
Get sign of 8-bit signed integer.
- `val`: Input value
- Returns: -1 if negative, 0 if zero, 1 if positive

#### `u16 abs16(s16 val) fastcall`
Absolute value of 16-bit signed integer.
- `val`: Input value
- Returns: Absolute value

#### `u8 abs8(s8 val) fastcall`
Absolute value of 8-bit signed integer.
- `val`: Input value
- Returns: Absolute value

#### `u8 max8(u8 a, u8 b)`
Maximum of two 8-bit unsigned integers.
- `a`, `b`: Values to compare
- Returns: Larger value

#### `u8 min8(u8 a, u8 b)`
Minimum of two 8-bit unsigned integers.
- `a`, `b`: Values to compare
- Returns: Smaller value

#### `u16 max16(u16 a, u16 b)`
Maximum of two 16-bit unsigned integers.
- `a`, `b`: Values to compare
- Returns: Larger value

#### `u16 min16(u16 a, u16 b)`
Minimum of two 16-bit unsigned integers.
- `a`, `b`: Values to compare
- Returns: Smaller value

**Math Utility Macros**:
```c
#define ABS(x)              // Absolute value
#define MAX(a,b)            // Maximum of two values
#define MIN(a,b)            // Minimum of two values
#define SIGN(x)             // Sign of value (-1, 0, 1)
#define APPR(val, target, amount)  // Approach target by amount
#define APPLY_SIGN(param, target)  // Apply sign of param to target
```

**Bit Manipulation Macros**:
```c
#define BITSET(val, bitmask)  // Set bits: val |= bitmask
#define BITCLR(val, bitmask)  // Clear bits: val &= ~bitmask
#define BITTST(val, bitmask)  // Test bits: val & bitmask
```

---

### Utilities - ZX0 Decompression

ZX0 is a space-efficient compression format optimized for Z80.

#### `void zx0_decompress(void* src, void* dst)`
Decompress ZX0-compressed data.
- `src`: Source address of compressed data
- `dst`: Destination address for decompressed data (must have sufficient space)

**ZX0 Resource Constants**:
```c
// Destination page numbers for various resources
#define DSTPAGE_LAYER2_0A    18
#define DSTPAGE_LAYER2_0B    19
#define DSTPAGE_LAYER2_1A    20
#define DSTPAGE_LAYER2_1B    21
#define DSTPAGE_LAYER2_2A    22
#define DSTPAGE_LAYER2_2B    23
#define DSTPAGE_LAYER2_3A    24
#define DSTPAGE_LAYER2_3B    25
#define DSTPAGE_LAYER2_4A    26
#define DSTPAGE_LAYER2_4B    27
#define DSTPAGE_TILEMAP_A    70
#define DSTPAGE_TILEMAP_B    71
#define DSTPAGE_COLLMAP_A    72
#define DSTPAGE_COLLMAP_B    73
#define DSTPAGE_LEVDATA      58
#define DSTPAGE_SPRITES_A    74
#define DSTPAGE_SPRITES_B    75
#define DSTPAGE_PALETTES     76

// Destination addresses
#define DSTADDR_TILESET_MAIN ((void*)0x6000)
#define DSTADDR_TILESET_TEXT ((void*)0x5000)
#define DSTADDR_DECOMP       ((void*)0x2000)

// Register numbers for bank switching during decompression
#define SRCSLOT_DECOMP_REG   0x50
#define DSTSLOT_DECOMP_REG   0x51
```

---

### Utilities - Next Hardware

ZX Spectrum Next-specific hardware control functions.

#### `void set_cpu_speed_28(void)`
Set CPU to maximum speed (28MHz).

#### `void disable_contention(void)`
Disable memory contention for maximum performance.

#### `void enable_dacs(void)`
Enable DAC audio output.

#### `void switchSlotPage8k(u8 slot, u8 page)`
Switch an 8KB memory slot to a specific page.
- `slot`: Slot number (0-7)
- `page`: Page number to map

#### `void selectTilemapPaletteFirst(void)`
Select first tilemap palette for operations.

#### `void selectTilemapPaletteSecond(void)`
Select second tilemap palette for operations.

#### `void disableULA(void)`
Disable the ULA (classic Spectrum) display layer.

#### `void disable_8x5_entries_for_extended_keys(void)`
Disable 8×5 keyboard matrix entries to enable Next extended keys.

#### `u16 getActiveVideoLineWord(void)`
Get current video scanline being drawn.
- Returns: Scanline number (0-311)

#### `u16 getLinesSinceBottomSync(void)`
Get number of scanlines since bottom vertical sync.
- Returns: Scanline count

#### `void waitForScanline(u16 targetLine)`
Wait until video reaches a specific scanline.
- `targetLine`: Scanline number to wait for

#### `void fillBitmapArea(u16 value)`
Fill classic Spectrum bitmap area ($4000-$57FF) with a value.
- `value`: 16-bit value to fill (fills bytes in pairs)

#### `void fillAttributeArea(u16 value)`
Fill classic Spectrum attribute area ($5800-$5BFF) with a value.
- `value`: 16-bit value to fill

#### `void fillTilemapArea(u16 value)`
Fill tilemap area ($4000-$4FFF) with a value.
- `value`: 16-bit value to fill (tile + attribute)

#### `u16 get_sp(void)`
Get current stack pointer value.
- Returns: SP register value

#### `void set_sp(u16 newval)`
Set stack pointer value.
- `newval`: New SP value

---

### Type Definitions

Basic type definitions used throughout the library:

```c
// Unsigned types
typedef unsigned char ubyte;
typedef unsigned char u8;
typedef unsigned short uword;
typedef unsigned short u16;

// Signed types
typedef signed char sbyte;
typedef signed char s8;
typedef signed short sword;
typedef signed short s16;

// Boolean (when not using stdbool.h)
typedef u8 bool;
#define true 1
#define false 0

// Function pointer
typedef void (*FunPtr)(void);
```

**Nibble Manipulation Macros**:
```c
#define HINIB(a_byte)       // Extract high nibble: byte >> 4
#define LONIB(a_byte)       // Extract low nibble: byte & 0xF
#define JOINIB(hi, lo)      // Join nibbles: (hi << 4) | lo
```

---

## Deep Explanation

### Architecture Overview

The ZX Spectrum Next is a modern reimplementation of the classic ZX Spectrum with extensive enhancements. NEXLIB is designed to make these enhancements accessible while maintaining efficient code generation for the Z80N processor.

#### Hardware Layers

The Next supports multiple independent display layers that can be composited:

1. **ULA Layer**: The original Spectrum display (32×24 character cells, 256×192 pixels)
2. **Layer 2**: High-resolution bitmap layer (up to 320×256 pixels, 8-bit color)
3. **Tilemap**: Character/tile-based layer (40×24 or 80×24 tiles)
4. **Sprites**: Up to 128 hardware sprites (16×16 or 8×8 pixels)

These layers are drawn in a configurable priority order with transparency support, allowing for sophisticated graphics effects.

#### Memory Architecture

The Next has 2MB of RAM organized as 256 banks of 8KB each. The Z80's 64KB address space is divided into 8 slots of 8KB:

```
Slot 0: $0000-$1FFF (8KB)
Slot 1: $2000-$3FFF (8KB)
Slot 2: $4000-$5FFF (8KB)
Slot 3: $6000-$7FFF (8KB)
Slot 4: $8000-$9FFF (8KB)
Slot 5: $A000-$BFFF (8KB)
Slot 6: $C000-$DFFF (8KB)
Slot 7: $E000-$FFFF (8KB)
```

The MMU (Memory Management Unit) allows any bank to be mapped into any slot, providing access to the full 2MB of RAM. NEXLIB provides paging functions to save/restore slot configurations and map banks.

#### Layer 2 Details

Layer 2 is a bitmap display layer with the following features:

- **Resolutions**: 256×192×8bpp, 320×256×8bpp, or 640×256×4bpp
- **Memory**: Typically uses banks 8-15 (48KB for 320×256×8bpp mode)
- **Palette**: 256 colors from 9-bit RGB333 palette (512 possible colors)
- **Scrolling**: Hardware horizontal and vertical scrolling
- **Banking**: Can page through different banks for scrolling backgrounds larger than one screen

The 320×256 mode extends beyond the visible area, allowing for hardware scrolling effects without software blitting.

#### Tilemap Details

The tilemap layer provides efficient character and tile-based graphics:

- **Modes**: 40-column (8×8 tiles) or 80-column (4×8 tiles)
- **Resolution**: 40×24 or 80×24 tiles (320×192 or 320×192 pixels)
- **Tile Definitions**: Up to 512 different 8×8 pixel tiles
- **Attributes**: Each tile has 8-bit attributes (palette offset, flip X/Y, rotate)
- **Memory**: Tilemap data and tile definitions stored in separate areas
- **Scrolling**: Hardware pixel-level scrolling in both directions

The tilemap is ideal for text displays, tile-based games, and UI elements.

#### Sprite System

The hardware sprite system supports:

- **Count**: Up to 128 sprites
- **Sizes**: 16×16 or 8×8 pixels
- **Colors**: 256 colors from sprite palette
- **Transformations**: X/Y mirroring and 90° rotation
- **Scaling**: 1×, 2×, 4×, or 8× in X and Y independently
- **Patterns**: Reusable sprite patterns (multiple sprites can share the same graphics)
- **Anchoring**: Sprites can be positioned relative to other sprites
- **Collision**: Hardware collision detection support

Sprites are defined using the `SpriteDef` structure and updated using `sprite_update_*` functions.

#### Palette System

Each graphics layer has its own 256-color palette:

- **Layer 2**: Palette 0
- **Sprites**: Palette 1
- **Tilemap**: Palette 2-3 (two palettes available)
- **ULA**: Uses first 16 colors of Layer 2 palette

Colors are 9-bit RGB333 format (3 bits per channel plus priority bit):
- Format: `%GGGRRRBBB` (bits 8-0)
- Stored as 2 bytes: LSB first, then MSB

The priority bit can be used to force a color to appear on top of other layers.

### Clipping System

NEXLIB provides centered clipping functions that automatically calculate clipping windows. The maximum viewing area is 320×256 pixels. When you request a centered clip region (e.g., 288×192), the library calculates the appropriate borders:

```
320 = 16 + 288 + 16 (horizontal)
256 = 32 + 192 + 32 (vertical)
```

This is useful for letterboxing or creating bordered displays. Each layer (Layer 2, sprites, tilemap, ULA) has independent clipping windows.

### Input System

#### Keyboard

The keyboard system provides edge detection (just pressed/just released) by maintaining state for current and previous frames. Call `keyb_update()` once per frame to update the state, then use the query functions.

The keyboard state is stored in 8 separate bytes representing different key groups, allowing efficient bit-testing. Key codes are 16-bit values where the high byte indicates the key group and the low byte is a bitmask for the specific key.

#### Joystick

Similar to keyboard, joystick state is maintained for current and previous frames, enabling edge detection. The system supports both Kempston and Sinclair joystick interfaces automatically.

### DMA Operations

The Next's DMA controller enables fast memory transfers without CPU intervention:

- **Burst Mode**: Transfers during active display time (slower but no visual artifacts)
- **Byte Mode**: Transfers during blanking periods (faster but may cause flicker)
- **Typical Speed**: ~1.75 cycles per byte at 28MHz (much faster than CPU memcpy)

DMA is particularly useful for:
- Loading sprite/tile data from ROM/RAM to VRAM
- Copying large Layer 2 bitmaps
- Playing digital audio samples

### Interrupt System

NEXLIB provides IM2 (Interrupt Mode 2) interrupt handling with two variants:

1. **FE Vector**: Places vector table at $FE00, leaving more stack space above
2. **FD Vector**: Places vector table at $FD00, alternative memory layout

The interrupt system includes:
- **Frame Counter**: Automatically incremented at 50Hz (PAL) or 60Hz (NTSC)
- **Callback System**: Assign custom handler to `on_interrupt_callback`
- **Scanline Interrupts**: Trigger interrupts at specific scanlines for raster effects

Example usage:
```c
void game_interrupt(void) {
    // Update frame counter
    // Update music/sound
    // Other per-frame tasks
}

void init(void) {
    on_interrupt_callback = game_interrupt;
    interrupt_init_FE();
}
```

### Screen Context Management

The screen context system (`ScrCtx`) provides a way to manage entire screen configurations including:

- Multiple Layer 2 banks (for parallax or multi-screen games)
- Tilesets (main and text)
- Palettes for each layer
- Spritesets
- Tilemaps and collision maps
- Level data

All data can be stored compressed (ZX0 format) and is automatically decompressed when loading a context. This allows efficient storage of multiple screens/levels.

### Screen Controller

The screen controller provides a simple state machine for managing game screens/scenes:

```c
void menu_entry(void) { /* Initialize menu */ }
void menu_update(void) { /* Update menu logic */ }
void menu_exit(void) { /* Clean up menu */ }

void game_entry(void) { /* Initialize game */ }
void game_update(void) { /* Update game logic */ }
void game_exit(void) { /* Clean up game */ }

// Start with menu
sc_switch_screen(menu_entry, menu_update, menu_exit);

// In your main loop
while(1) {
    sc_update();  // Calls current screen's update function
}

// Switch to game (from menu_update)
if (start_pressed) {
    sc_switch_screen(game_entry, game_update, game_exit);
}
```

The controller handles calling exit functions, switching screens, and calling entry functions automatically.

### Performance Considerations

1. **fastcall Convention**: Functions with a single parameter use Z80 registers (HL for pointers/words, A for bytes) instead of the stack, reducing overhead significantly.

2. **Assembly Implementations**: Performance-critical functions (sprite updates, DMA operations, printing) use hand-optimized Z80 assembly.

3. **Batch Operations**: Update multiple sprites/tiles, then wait for VSync to avoid tearing. The interrupt system helps synchronize with display.

4. **DMA for Bulk Transfers**: Always use DMA for transfers >64 bytes. The DMA controller is much faster than CPU-based copying.

5. **Bank Switching Overhead**: Minimize calls to paging functions. If you need to access multiple banks, organize code to batch bank switches.

6. **CPU Speed**: Call `set_cpu_speed_28()` for maximum performance (28MHz). Remember that at higher speeds, you may need to adjust timing-dependent code.

### Common Patterns

#### Double Buffering (Layer 2)
```c
u8 back_buffer = 8;
u8 front_buffer = 9;

while (game_running) {
    // Draw to back buffer
    layer2_set_ram_page(back_buffer);
    draw_game_frame();
    
    // Wait for VSync and swap
    // (assuming interrupt system is set up)
    // Wait for vblank...
    layer2_set_ram_page(front_buffer);
    
    // Swap buffer pointers
    u8 temp = back_buffer;
    back_buffer = front_buffer;
    front_buffer = temp;
}
```

#### Sprite Animation
```c
SpriteDef player;
u8 anim_frame = 0;
u8 anim_counter = 0;

player.slot = 0;
player.x = 100;
player.y = 100;
player.pat = 0;  // First frame
player.scale = SCALE_X1 | SCALE_Y1;
sprite_update(&player);

// In game loop
anim_counter++;
if (anim_counter >= 5) {  // Change frame every 5 frames
    anim_counter = 0;
    anim_frame = (anim_frame + 1) % 4;  // 4-frame animation
    player.pat = anim_frame * 4;  // 4 patterns per frame
    sprite_update(&player);
}
```

#### Scrolling Tilemap
```c
s16 scroll_x = 0;
s16 scroll_speed = 2;

while (game_running) {
    scroll_x += scroll_speed;
    if (scroll_x >= 320) scroll_x = 0;  // Wrap at 320 pixels
    
    tilemap_set_hoffset(scroll_x);
    
    // Wait for next frame...
}
```

#### Input Handling with Edge Detection
```c
// In game loop after keyb_update()
if (keyb_is_just_pressed(KEY_ZX_SPACE)) {
    // Space was just pressed this frame
    player_jump();
}

if (keyb_is_pressed(KEY_ZX_O)) {
    // O is held down
    player_move_left();
}

if (keyb_is_just_released(KEY_ZX_P)) {
    // P was just released
    player_stop_moving();
}
```

### Integration with z88dk

NEXLIB is designed to work with the z88dk toolchain:

```makefile
CC = zcc
CFLAGS = +zxn -vn -SO3 -compiler=sdcc -clib=sdcc_iy
LDFLAGS = -startup=31 -subtype=nex

NEXLIB_PATH = ../nexlib
LIBS = -L$(NEXLIB_PATH) -lnexlib

game.nex: main.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o game main.c $(LIBS)
```

Use z88dk's standard library functions (string.h, stdlib.h, stdio.h) alongside NEXLIB functions.

### Debugging Techniques

1. **Print Functions**: Use `print_*` and `DBG*` macros to display values on screen
2. **Border Color**: Quick visual debugging using NextReg 0x43
3. **Sprite Positions**: Display sprites at known positions to verify sprite system
4. **Keyboard Debug**: Use `keyb_print_debug()` to see keyboard state

### Building for the Next

Use the z88dk toolchain with the `+zxn` target:

```bash
# Compile a simple program
zcc +zxn -vn -SO3 -startup=31 -clib=sdcc_iy \
    -subtype=nex -o game.nex main.c -lnexlib
```

For debugging, use emulators like CSpect or ZEsarUX.

### Best Practices

1. **Initialize Systems**: Always call init functions before use
2. **Update Input**: Call input update functions once per frame
3. **Synchronize with VSync**: Use interrupts or wait for VBlank
4. **Manage Stack**: Ensure sufficient stack space (256-512 bytes minimum)
5. **Test on Hardware**: Always test on real Next hardware before release
6. **Profile Code**: Use border color tricks to identify slow sections
7. **Compress Assets**: Use ZX0 compression for graphics/level data

---

## Appendix: Complete Example

Here's a minimal complete program using NEXLIB:

```c
#include <arch/zxn.h>
#include <interrupt.h>
#include <layer2.h>
#include <sprite_manager.h>
#include <keyb.h>

SpriteDef player;
u8 frame_counter = 0;

void game_interrupt(void) {
    frame_counter++;
}

void main(void) {
    // Initialize systems
    interrupt_init_FE();
    on_interrupt_callback = game_interrupt;
    
    keyb_init();
    sprite_setup();
    layer2_init();
    
    // Set up player sprite
    player.slot = 0;
    player.x = 160;
    player.y = 128;
    player.pat = 0;
    player.pal = 0;
    player.mirrot = 0;
    player.scale = SCALE_X1 | SCALE_Y1;
    sprite_update(&player);
    
    // Game loop
    while (1) {
        // Update input
        keyb_update();
        
        // Move player
        if (keyb_is_pressed(KEY_ZX_O)) player.x -= 2;
        if (keyb_is_pressed(KEY_ZX_P)) player.x += 2;
        
        // Update sprite
        sprite_update(&player);
        
        // Wait for next frame
        while (frame_counter == 0);
        frame_counter = 0;
    }
}
```

---

## Conclusion

NEXLIB provides a comprehensive, performance-oriented interface to the ZX Spectrum Next hardware. By abstracting hardware complexity while maintaining low-level access when needed, it enables developers to create sophisticated games and applications efficiently.

---

**NEXLIB** - (c) 2024-2025 David Crespo  
https://github.com/dcrespo3d  
https://davidprograma.itch.io  
https://www.youtube.com/@Davidprograma
