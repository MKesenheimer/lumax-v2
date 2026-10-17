/*
 * probe.c - low-level diagnostic for the connected Lumax card.
 *
 * Applies the reference (liblumax.so) open setup - 1000ms USB read/write
 * timeouts, 2ms latency timer, the reset sequence (256 zero bytes + "GNU"),
 * a settle delay, and an RX/TX purge - then drives the real g4jz ID read
 * command and dumps whatever the card sends back. Used to determine whether
 * the device is responsive at the protocol level.
 *
 * Build:  gcc -o probe probe.c $(pkg-config --cflags --libs libftdi1)
 * Run:    ./probe [serial]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libftdi1/ftdi.h>

static void hexdump(const char *tag, const unsigned char *p, int n)
{
    printf("%s (%d bytes): ", tag, n);
    for (int i = 0; i < n; i++)
        printf("%02x ", p[i]);
    printf("\n");
}

int main(int argc, char **argv)
{
    const char *serial = (argc > 1) ? argv[1] : NULL;

    struct ftdi_context *ftdi = ftdi_new();
    if (!ftdi) { printf("ftdi_new failed\n"); return 1; }

    int rc = ftdi_usb_open_desc(ftdi, 0x403, 0xC88A, NULL, serial);
    printf("open(serial=%s) = %d\n", serial ? serial : "(first)", rc);
    if (rc < 0) { printf("  err: %s\n", ftdi_get_error_string(ftdi)); ftdi_free(ftdi); return 1; }

    /* Reference setup: FT_SetTimeouts(h,1000,1000), FT_SetLatencyTimer(h,2) */
    ftdi->usb_read_timeout = 1000;
    ftdi->usb_write_timeout = 1000;
    ftdi_set_latency_timer(ftdi, 2);
    printf("ftdi type = %d (0=TYPE_B 1=2232C 2=R 3=2232H 4=4232H)\n", ftdi->type);
    printf("set read/write timeout=1000ms, latency=2ms\n");

    /* Reset: reference P5555 writes 256 zero bytes, 256 times = 65536 zeros,
       then usleep(200ms), then purge RX/TX. (The "GNU" in the decompilation
       was IDA mislabeling the 0x100 amount immediate as the 0x100 string ptr.) */
    unsigned char zbuf[256];
    memset(zbuf, 0, sizeof(zbuf));
    int wr = 0;
    for (int rep = 0; rep < 256; rep++) {
        int w = ftdi_write_data(ftdi, zbuf, 256);
        if (w < 0) { printf("reset write rep %d failed: %d\n", rep, w); break; }
        wr += w;
    }
    printf("reset write total = %d (expect 65536)\n", wr);
    usleep(200000); /* 200ms settle, matches reference */
    ftdi_usb_purge_buffers(ftdi);
    printf("purged RX/TX after reset\n");

    /* Drain anything that trickles in right after reset (up to ~1s total) */
    unsigned char drain[256];
    int total_drain = 0;
    for (int t = 0; t < 20; t++) {
        int d = ftdi_read_data(ftdi, drain, sizeof(drain));
        if (d > 0) { hexdump("post-reset drain", drain, d); total_drain += d; }
        if (d <= 0) break;
    }
    if (total_drain == 0) printf("post-reset: no data received\n");

    /* g4jz: write [0xC9, 0,0,0,0,0, 0xC9], wait 50ms, then read in stages
       exactly like the reference: 1 byte (echo), then up to 17 more bytes
       (16 data + 1 checksum). The echo and data arrive as separate USB
       packets, so a single read would miss the data. Poll until 16 real
       bytes arrive. */
    unsigned char cmd[7] = {0xC9, 0, 0, 0, 0, 0, 0xC9};
    int got_data = 0;
    ftdi->usb_read_timeout = 120; /* short: catch a ready response, else time out */
    for (int attempt = 0; attempt < 8 && !got_data; attempt++) {
        ftdi_write_data(ftdi, cmd, 7);
        usleep(50000); /* 50ms, matches reference */

        unsigned char acc[18];
        int total = 0;
        for (;;) { /* drain everything that arrives for this command */
            int r = ftdi_read_data(ftdi, acc + total, 18 - total);
            if (r <= 0) break;
            total += r;
            if (total >= 18) break;
        }
        if (total > 0) {
            printf("attempt %d: %d byte", attempt, total);
            for (int k = 0; k < total && k < 18; k++) printf(" %02x", acc[k]);
            printf("\n");
        }
        if (total >= 18 && acc[0] == 0xC9) {
            int x = 0;
            for (int k = 1; k <= 16; k++) x ^= acc[k];
            printf("  => checksum: data_xor=0x%02x card=0x%02x %s\n",
                   x, acc[17], (x == acc[17]) ? "OK" : "MISMATCH");
            got_data = 1;
        }
        /* else: only the echo arrived -> not ready, poll again */
    }
    if (!got_data)
        printf("g4jz: card echoes 0xC9 but never sends 16 data bytes in %d attempts\n", 30);

    /* ---- Functional command tests ----
       These are the real "does the card respond" checks. */
    ftdi_usb_purge_buffers(ftdi);
    ftdi->usb_read_timeout = 400;

    /* Test A: SetTTL(0x55) -> [0x1E,0x55,0,0,0,0,0x55^0x1E=0x4B], expect 1 byte 0x4B */
    printf("\n--- SetTTL(0x55) test ---\n");
    unsigned char ttl_cmd[7] = {0x1E, 0x55, 0, 0, 0, 0, (unsigned char)(0x55 ^ 0x1E)};
    ftdi_write_data(ftdi, ttl_cmd, 7);
    usleep(50000);
    unsigned char ttl_resp[4];
    int tr = 0;
    for (int k = 0; k < 3; k++) { int r = ftdi_read_data(ftdi, ttl_resp + tr, 4 - tr); if (r > 0) tr += r; else break; }
    printf("SetTTL: read %d byte", tr);
    for (int k = 0; k < tr && k < 4; k++) printf(" %02x", ttl_resp[k]);
    printf("  (expect first=0x%02x)\n", 0x55 ^ 0x1E);

    /* Test B: WaitForBuffer -> [0x04,0,0,0,0,0,0x04], expect 4-byte reply starting 0x04 */
    printf("\n--- WaitForBuffer test ---\n");
    unsigned char wb_cmd[7] = {0x04, 0, 0, 0, 0, 0, 0x04};
    ftdi_write_data(ftdi, wb_cmd, 7);
    usleep(50000);
    unsigned char wb_resp[4] = {0, 0, 0, 0};
    int wrb = 0;
    for (int k = 0; k < 4; k++) { int r = ftdi_read_data(ftdi, wb_resp + wrb, 4 - wrb); if (r > 0) wrb += r; else break; }
    printf("WaitForBuffer: read %d byte", wrb);
    for (int k = 0; k < wrb && k < 4; k++) printf(" %02x", wb_resp[k]);
    printf("  (expect first=0x04, 4 bytes total)\n");

    ftdi_usb_close(ftdi);
    ftdi_free(ftdi);
    return 0;
}
