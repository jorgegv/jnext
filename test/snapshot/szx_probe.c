/* A FOREIGN reader for jnext's `.szx` — GH #27 stage S9, design §13.2(1).
 *
 * WHY THIS EXISTS. §13.1: jnext's `.szx` saver once wrote RAM pages 0-111 and
 * its own loader accepted any `uint8_t` page with no upper bound, so
 * save → load → compare passed byte-exact, with discriminative,
 * mutation-tested assertions — all green, all worthless. libspectrum's
 * `read_ramp_chunk()` hard-rejects any page > 63, so EVERY `.szx` file jnext
 * could produce failed to load in real FUSE. Saver and loader shared the blind
 * spot, which made the defect structurally invisible to the suite as written.
 *
 * WHAT MAKES THIS DIFFERENT FROM EVERY OTHER TEST IN THE TREE. It links
 * **libspectrum** — the library FUSE itself uses, which we did not write and
 * cannot accidentally make permissive. It is the only adjudicator in this
 * project that can say "the file jnext produced is not a valid snapshot"
 * without consulting any jnext code.
 *
 * WHY NOT DRIVE FUSE'S DEBUGGER, which §13.2(1) describes. That route works —
 * `--debugger-command` is real, is in `man fuse` and not in `--help`, and runs
 * before emulator startup — but it needs Xvfb, a GTK UI, a breakpoint that
 * must be hit, and output scraped from a GUI process. Every one of those is a
 * way for the row to produce NOTHING and compare nothing against nothing,
 * which §13.2(1) itself names as the failure most worth avoiding: it converts
 * the best evidence in §13.2 into the most confident lie. Linking the same
 * library directly removes all four risks and strengthens the claim, because
 * the registers come back as values rather than as scraped text.
 *
 * Prints one `key=value` line per field, or a diagnostic on stderr and a
 * non-zero exit. Built only when libspectrum is present (see test/CMakeLists).
 */

#include <libspectrum.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: szx_probe FILE.szx\n");
        return 2;
    }

    if (libspectrum_init() != LIBSPECTRUM_ERROR_NONE) {
        fprintf(stderr, "szx_probe: libspectrum_init failed\n");
        return 2;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    if (fseek(f, 0, SEEK_END) != 0) { perror(argv[1]); fclose(f); return 2; }
    long n = ftell(f);
    if (n <= 0) { fprintf(stderr, "szx_probe: %s is empty\n", argv[1]);
                  fclose(f); return 2; }
    rewind(f);

    unsigned char *buf = malloc((size_t)n);
    if (!buf) { fclose(f); return 2; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "szx_probe: short read of %s\n", argv[1]);
        free(buf); fclose(f); return 2;
    }
    fclose(f);

    libspectrum_snap *snap = libspectrum_snap_alloc();
    if (!snap) { free(buf); return 2; }

    /* THE LINE THAT MATTERS. This is libspectrum's own reader, with its own
     * bounds — the one that rejected every .szx jnext used to write. A file
     * jnext can produce and this call refuses is the §13.1 defect, exactly. */
    libspectrum_error err =
        libspectrum_snap_read(snap, buf, (size_t)n, LIBSPECTRUM_ID_SNAPSHOT_SZX,
                              argv[1]);
    if (err != LIBSPECTRUM_ERROR_NONE) {
        fprintf(stderr, "szx_probe: libspectrum REFUSED %s: error %d\n",
                argv[1], (int)err);
        libspectrum_snap_free(snap);
        free(buf);
        return 1;
    }

    printf("machine=%d\n", (int)libspectrum_snap_machine(snap));
    printf("pc=%u\n",   (unsigned)libspectrum_snap_pc(snap));
    printf("sp=%u\n",   (unsigned)libspectrum_snap_sp(snap));
    printf("a=%u\n",    (unsigned)libspectrum_snap_a(snap));
    printf("f=%u\n",    (unsigned)libspectrum_snap_f(snap));
    printf("bc=%u\n",   (unsigned)libspectrum_snap_bc(snap));
    printf("de=%u\n",   (unsigned)libspectrum_snap_de(snap));
    printf("hl=%u\n",   (unsigned)libspectrum_snap_hl(snap));
    printf("i=%u\n",    (unsigned)libspectrum_snap_i(snap));
    printf("im=%u\n",   (unsigned)libspectrum_snap_im(snap));
    printf("iff1=%u\n", (unsigned)libspectrum_snap_iff1(snap));
    printf("iff2=%u\n", (unsigned)libspectrum_snap_iff2(snap));

    libspectrum_snap_free(snap);
    free(buf);
    return 0;
}
