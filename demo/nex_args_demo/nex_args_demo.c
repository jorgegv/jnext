/*
 * NEX V1.3 CLI-buffer demo (GH #172)
 *
 * Prints the argument line jnext delivered through the NEX V1.3 CLI
 * buffer, so `--nex-args "..."` is visible in a screenshot:
 *
 *   jnext --headless --load nex_args_demo.nex --nex-args "HELLO NEXT" \
 *         --delayed-screenshot out.png --delayed-screenshot-frames 60 \
 *         --delayed-automatic-exit-frames 90
 *
 * The buffer address and size are the ones the patched header declares
 * (see make-v13.py: 0xBF00, 32 bytes) — a compile-time constant here,
 * because the C runtime clobbers DE long before main() could read it.
 * The loader sets DE to the same address; that half is proven by the
 * NEXV13-CLI-01/04 unit rows, which can read DE at the instant of entry.
 *
 * Text is drawn with the ROM character set at $3D00, the same route
 * demo/rom_charset_test uses (available under --load since Task 20).
 */

#pragma output REGISTER_SP  = 0xfffd
#pragma output CRT_ORG_CODE = 0x8000

#define SCREEN_ATTR ((unsigned char *)0x5800)
#define SCREEN_PIX  ((unsigned char *)0x4000)
#define ROM_CHARSET ((unsigned char *)0x3D00)

/* Must match the CLIBUFFER / CLIBUFFERSIZE fields make-v13.py writes at
 * header offsets 148 / 150. */
#define CLI_BUFFER      ((unsigned char *)0xBF00)
#define CLI_BUFFER_SIZE 32

static void plot_char(unsigned char cx, unsigned char cy, unsigned char ch)
{
    unsigned char row;
    const unsigned char *gdata;

    if (ch < 32 || ch > 127) ch = '?';
    gdata = &ROM_CHARSET[(ch - 32) * 8];

    for (row = 0; row < 8; row++) {
        unsigned int addr = 0x4000u
            | ((unsigned int)(cy & 0x18) << 8)
            | ((unsigned int)row << 8)
            | ((unsigned int)(cy & 7) << 5)
            | cx;
        *((unsigned char *)addr) = gdata[row];
    }
    SCREEN_ATTR[cy * 32u + cx] = 0x47;  /* BRIGHT white on black */
}

static unsigned char plot_str(unsigned char cx, unsigned char cy, const char *s)
{
    while (*s && cx < 32) plot_char(cx++, cy, (unsigned char)*s++);
    return cx;
}

static void cls_to_black(void)
{
    unsigned int i;
    for (i = 0; i < 6144; i++) SCREEN_PIX[i]  = 0;
    for (i = 0; i < 768;  i++) SCREEN_ATTR[i] = 0x07;
}

int main(void)
{
    unsigned char i;

    cls_to_black();

    plot_str(0, 0, "NEX V1.3 CLI BUFFER");

    /* Count the line first, so its length can be printed on the label
     * row and the text itself gets a full 32-column row of its own — a
     * buffer-sized line then needs no wrapping. LEN also disambiguates
     * "the line is empty" from "nothing was printed at all". */
    for (i = 0; i < CLI_BUFFER_SIZE && CLI_BUFFER[i] != 0; i++) { }

    plot_str(0, 2, "ARGS, LEN=");
    plot_char(10, 2, (unsigned char)('0' + i / 10));
    plot_char(11, 2, (unsigned char)('0' + i % 10));
    plot_char(12, 2, ':');

    for (i = 0; i < CLI_BUFFER_SIZE && CLI_BUFFER[i] != 0; i++)
        plot_char(i, 3, CLI_BUFFER[i]);

    for (;;) { }
}
