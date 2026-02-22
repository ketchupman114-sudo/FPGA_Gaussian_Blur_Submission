#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#define WIDTH  320
#define HEIGHT 240
#define TOTAL_PIXELS (WIDTH * HEIGHT)

static int parse_rgb565_line(const char *line, uint16_t *out)
{
    // Skip leading whitespace
    while (*line && isspace((unsigned char)*line)) line++;

    // Skip comment/header lines
    if (line[0] == '/' && line[1] == '/') return 0;

    // Trim trailing whitespace/newline by copying into a buffer
    char buf[64];
    size_t n = 0;
    while (line[n] && line[n] != '\r' && line[n] != '\n' && n < sizeof(buf)-1) {
        buf[n] = line[n];
        n++;
    }
    buf[n] = '\0';

    // Empty line
    if (n == 0) return 0;

    // Reject unknowns like xxxx/XXXX
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == 'x' || buf[i] == 'X') return 0;
    }

    // Must be 1..4 hex digits only (no extra tokens)
    for (size_t i = 0; i < n; i++) {
        if (!isxdigit((unsigned char)buf[i])) return 0;
    }
    if (n > 4) return 0;

    unsigned long v = strtoul(buf, NULL, 16);
    *out = (uint16_t)(v & 0xFFFF);
    return 1;
}

int main(void)
{
    FILE *hex_file = fopen("blurred.hex", "r");
    if (!hex_file) {
        perror("blurred.hex");
        return 1;
    }

    FILE *ppm_file = fopen("output.ppm", "wb");
    if (!ppm_file) {
        perror("output.ppm");
        fclose(hex_file);
        return 1;
    }

    fprintf(ppm_file, "P6\n%d %d\n255\n", WIDTH, HEIGHT);

    char line[256];
    int written = 0;

    uint16_t pixel = 0;
    uint16_t last_valid = 0;

    while (written < TOTAL_PIXELS && fgets(line, sizeof(line), hex_file)) {
        if (!parse_rgb565_line(line, &pixel)) {
            continue; // skip headers/xxxx/blank/bad lines safely
        }

        last_valid = pixel;

        // RGB565 -> RGB888
        uint8_t r5 = (pixel >> 11) & 0x1F;
        uint8_t g6 = (pixel >> 5)  & 0x3F;
        uint8_t b5 =  pixel        & 0x1F;

        uint8_t r8 = (uint8_t)((r5 * 255) / 31);
        uint8_t g8 = (uint8_t)((g6 * 255) / 63);
        uint8_t b8 = (uint8_t)((b5 * 255) / 31);

        fputc(r8, ppm_file);
        fputc(g8, ppm_file);
        fputc(b8, ppm_file);

        written++;
    }

    // If file ended early, fill remaining pixels with last valid (optional)
    while (written < TOTAL_PIXELS) {
        uint16_t p = last_valid;

        uint8_t r5 = (p >> 11) & 0x1F;
        uint8_t g6 = (p >> 5)  & 0x3F;
        uint8_t b5 =  p        & 0x1F;

        uint8_t r8 = (uint8_t)((r5 * 255) / 31);
        uint8_t g8 = (uint8_t)((g6 * 255) / 63);
        uint8_t b8 = (uint8_t)((b5 * 255) / 31);

        fputc(r8, ppm_file);
        fputc(g8, ppm_file);
        fputc(b8, ppm_file);

        written++;
    }

    fclose(hex_file);
    fclose(ppm_file);

    printf("Wrote output.ppm (%d pixels)\n", TOTAL_PIXELS);
    return 0;
}