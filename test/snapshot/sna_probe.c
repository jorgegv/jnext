/* A FOREIGN reader for jnext's `.sna` — GH #274.
 *
 * WHY THIS EXISTS. The sibling szx_probe.c records the defect class: jnext's
 * `.szx` saver once wrote RAM pages its own loader happily accepted, so
 * save -> load -> compare passed byte-exact while libspectrum — and therefore
 * real FUSE — rejected every file jnext could produce. Saver and loader shared
 * the blind spot, which made the defect structurally invisible.
 *
 * GH #274 makes jnext write the 128K SNA form for the first time, and it is
 * written to be the exact inverse of jnext's OWN SnaLoader. That is exactly the
 * pairing §13.1 warns about: an offset both sides agree on, or a bank order
 * both sides get wrong, round-trips perfectly and interoperates with nothing.
 * libspectrum is the adjudicator that does not consult jnext code.
 *
 * WHAT IT PRINTS. One `key=value` line per field: the machine libspectrum
 * decided the file describes, the CPU state, the 128K paging byte it read from
 * the extended header, and a per-bank checksum of the eight 16K RAM pages so
 * the BANK CONTENT and BANK ORDER are adjudicated too — the state the old
 * 48K-only saver silently dropped, and the part a header-only comparison would
 * miss. A missing page prints `bankN=absent`, which is a fact worth seeing
 * rather than a zero to be confused with an empty bank.
 *
 * Prints a diagnostic on stderr and exits non-zero when libspectrum REFUSES the
 * file — that refusal is the whole point of the row that drives this.
 * Built only when libspectrum is present (see test/CMakeLists.txt).
 */

#include <libspectrum.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A cheap, order-sensitive checksum: the same bytes in a different place give a
 * different value, which is what makes a swapped pair of banks visible. */
static unsigned long page_sum(const libspectrum_byte *p, size_t n)
{
    unsigned long h = 5381;
    for (size_t i = 0; i < n; i++) h = h * 33u + p[i];
    return h;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: sna_probe FILE.sna\n");
        return 2;
    }

    if (libspectrum_init() != LIBSPECTRUM_ERROR_NONE) {
        fprintf(stderr, "sna_probe: libspectrum_init failed\n");
        return 2;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    if (fseek(f, 0, SEEK_END) != 0) { perror(argv[1]); fclose(f); return 2; }
    long n = ftell(f);
    if (n <= 0) { fprintf(stderr, "sna_probe: %s is empty\n", argv[1]);
                  fclose(f); return 2; }
    rewind(f);

    unsigned char *buf = malloc((size_t)n);
    if (!buf) { fclose(f); return 2; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "sna_probe: short read of %s\n", argv[1]);
        free(buf); fclose(f); return 2;
    }
    fclose(f);

    libspectrum_snap *snap = libspectrum_snap_alloc();
    if (!snap) { free(buf); return 2; }

    /* THE LINE THAT MATTERS: libspectrum's own SNA reader, with its own idea of
     * how long each form is and which bank each block belongs to. */
    libspectrum_error err =
        libspectrum_snap_read(snap, buf, (size_t)n, LIBSPECTRUM_ID_SNAPSHOT_SNA,
                              argv[1]);
    if (err != LIBSPECTRUM_ERROR_NONE) {
        fprintf(stderr, "sna_probe: libspectrum REFUSED %s: error %d\n",
                argv[1], (int)err);
        libspectrum_snap_free(snap);
        free(buf);
        return 1;
    }

    printf("size=%ld\n", n);
    printf("machine=%d\n", (int)libspectrum_snap_machine(snap));
    printf("pc=%u\n",   (unsigned)libspectrum_snap_pc(snap));
    printf("sp=%u\n",   (unsigned)libspectrum_snap_sp(snap));
    printf("a=%u\n",    (unsigned)libspectrum_snap_a(snap));
    printf("f=%u\n",    (unsigned)libspectrum_snap_f(snap));
    printf("bc=%u\n",   (unsigned)libspectrum_snap_bc(snap));
    printf("de=%u\n",   (unsigned)libspectrum_snap_de(snap));
    printf("hl=%u\n",   (unsigned)libspectrum_snap_hl(snap));
    printf("ix=%u\n",   (unsigned)libspectrum_snap_ix(snap));
    printf("iy=%u\n",   (unsigned)libspectrum_snap_iy(snap));
    printf("i=%u\n",    (unsigned)libspectrum_snap_i(snap));
    printf("r=%u\n",    (unsigned)libspectrum_snap_r(snap));
    printf("im=%u\n",   (unsigned)libspectrum_snap_im(snap));
    printf("iff1=%u\n", (unsigned)libspectrum_snap_iff1(snap));
    printf("iff2=%u\n", (unsigned)libspectrum_snap_iff2(snap));
    printf("border=%u\n", (unsigned)libspectrum_snap_out_ula(snap) & 0x07u);
    printf("port7ffd=%u\n",
           (unsigned)libspectrum_snap_out_128_memoryport(snap));

    for (int page = 0; page < 8; page++) {
        libspectrum_byte *p = libspectrum_snap_pages(snap, page);
        if (p) printf("bank%d=%lu\n", page, page_sum(p, 0x4000));
        else   printf("bank%d=absent\n", page);
    }

    libspectrum_snap_free(snap);
    free(buf);
    return 0;
}
