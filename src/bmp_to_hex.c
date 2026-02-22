#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUT_WIDTH 320
#define OUT_HEIGHT 240

// -------------------------------------------------------
// BMP Header Structs
// -------------------------------------------------------
#pragma pack(push, 1)
typedef struct {
  uint16_t bfType;
  uint32_t bfSize;
  uint16_t bfReserved1;
  uint16_t bfReserved2;
  uint32_t bfOffBits;
} BMPFileHeader;

typedef struct {
  uint32_t biSize;
  int32_t biWidth;
  int32_t biHeight;
  uint16_t biPlanes;
  uint16_t biBitCount;
  uint32_t biCompression;
  uint32_t biSizeImage;
  int32_t biXPelsPerMeter;
  int32_t biYPelsPerMeter;
  uint32_t biClrUsed;
  uint32_t biClrImportant;
} BMPInfoHeader;
#pragma pack(pop)

// -------------------------------------------------------
// RGB pixel struct
// -------------------------------------------------------
typedef struct {
  uint8_t r, g, b;
} Pixel;

// -------------------------------------------------------
// Convert 8-bit R,G,B → 16-bit RGB565
// Layout: [15:11]=R, [10:5]=G, [4:0]=B
// -------------------------------------------------------
static inline uint16_t to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t r5 = (r >> 3) & 0x1F;
  uint16_t g6 = (g >> 2) & 0x3F;
  uint16_t b5 = (b >> 3) & 0x1F;
  return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

// -------------------------------------------------------
// Bilinear resize: src (src_w x src_h) → dst (dst_w x dst_h)
// -------------------------------------------------------
Pixel *resize_bilinear(Pixel *src, int src_w, int src_h, int dst_w, int dst_h) {
  Pixel *dst = (Pixel *)malloc(dst_w * dst_h * sizeof(Pixel));
  if (!dst) return NULL;

  float x_scale = (float)src_w / dst_w;
  float y_scale = (float)src_h / dst_h;

  for (int dst_y = 0; dst_y < dst_h; dst_y++) {
    for (int dst_x = 0; dst_x < dst_w; dst_x++) {
      float src_xf = dst_x * x_scale;
      float src_yf = dst_y * y_scale;

      int x0 = (int)src_xf;
      int y0 = (int)src_yf;
      int x1 = x0 + 1 < src_w ? x0 + 1 : x0;
      int y1 = y0 + 1 < src_h ? y0 + 1 : y0;

      float dx = src_xf - x0;
      float dy = src_yf - y0;

      Pixel p00 = src[y0 * src_w + x0];
      Pixel p10 = src[y0 * src_w + x1];
      Pixel p01 = src[y1 * src_w + x0];
      Pixel p11 = src[y1 * src_w + x1];

      // Bilinear interpolation for each channel
      dst[dst_y * dst_w + dst_x].r =
          (uint8_t)(p00.r * (1 - dx) * (1 - dy) + p10.r * dx * (1 - dy) +
                    p01.r * (1 - dx) * dy + p11.r * dx * dy);
      dst[dst_y * dst_w + dst_x].g =
          (uint8_t)(p00.g * (1 - dx) * (1 - dy) + p10.g * dx * (1 - dy) +
                    p01.g * (1 - dx) * dy + p11.g * dx * dy);
      dst[dst_y * dst_w + dst_x].b =
          (uint8_t)(p00.b * (1 - dx) * (1 - dy) + p10.b * dx * (1 - dy) +
                    p01.b * (1 - dx) * dy + p11.b * dx * dy);
    }
  }
  return dst;
}

// -------------------------------------------------------
// Main
// -------------------------------------------------------
int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s input.bmp output.hex\n", argv[0]);
    fprintf(stderr, "  input.bmp  : any 24-bit BMP (any resolution)\n");
    fprintf(stderr,
            "  output.hex : RGB565 hex file for $readmemh in Verilog\n");
    return 1;
  }

  // -------------------------------------------------------
  // Open BMP and read headers
  // -------------------------------------------------------
  FILE *fp = fopen(argv[1], "rb");
  if (!fp) {
    perror("Cannot open input BMP");
    return 1;
  }

  BMPFileHeader fh;
  BMPInfoHeader ih;
  fread(&fh, sizeof(BMPFileHeader), 1, fp);
  fread(&ih, sizeof(BMPInfoHeader), 1, fp);

  if (fh.bfType != 0x4D42) {
    fprintf(stderr, "Error: Not a valid BMP file.\n");
    fclose(fp);
    return 1;
  }
  if (ih.biBitCount != 24) {
    fprintf(stderr, "Error: Only 24-bit BMP is supported. Got %d-bit.\n",
            ih.biBitCount);
    fclose(fp);
    return 1;
  }
  if (ih.biCompression != 0) {
    fprintf(stderr, "Error: Compressed BMP not supported.\n");
    fclose(fp);
    return 1;
  }

  int src_w = abs(ih.biWidth);
  int src_h = abs(ih.biHeight);
  int top_down = (ih.biHeight < 0);  // negative height = top-down storage

  printf("Input image: %dx%d pixels (24-bit BMP)\n", src_w, src_h);

  // -------------------------------------------------------
  // Read all pixel rows into a flat Pixel array
  // -------------------------------------------------------
  int row_stride = (src_w * 3 + 3) & ~3;  // BMP rows padded to 4 bytes
  uint8_t *row_buf = (uint8_t *)malloc(row_stride);
  Pixel *src_pixels = (Pixel *)malloc(src_w * src_h * sizeof(Pixel));
  if (!row_buf || !src_pixels) {
    fprintf(stderr, "Memory allocation failed.\n");
    return 1;
  }

  fseek(fp, fh.bfOffBits, SEEK_SET);

  for (int row = 0; row < src_h; row++) {
    fread(row_buf, 1, row_stride, fp);

    // BMP stores bottom-up by default (unless top_down flag set)
    int dst_row = top_down ? row : (src_h - 1 - row);

    for (int col = 0; col < src_w; col++) {
      // BMP channel order is B, G, R
      src_pixels[dst_row * src_w + col].r = row_buf[col * 3 + 2];
      src_pixels[dst_row * src_w + col].g = row_buf[col * 3 + 1];
      src_pixels[dst_row * src_w + col].b = row_buf[col * 3 + 0];
    }
  }
  fclose(fp);
  free(row_buf);

  // -------------------------------------------------------
  // Resize to 320x240 (skip if already correct size)
  // -------------------------------------------------------
  Pixel *out_pixels = NULL;

  if (src_w == OUT_WIDTH && src_h == OUT_HEIGHT) {
    printf("Image already 320x240, skipping resize.\n");
    out_pixels = src_pixels;
  } else {
    printf("Resizing to %dx%d using bilinear interpolation...\n", OUT_WIDTH,
           OUT_HEIGHT);
    out_pixels =
        resize_bilinear(src_pixels, src_w, src_h, OUT_WIDTH, OUT_HEIGHT);
    free(src_pixels);
    if (!out_pixels) {
      fprintf(stderr, "Resize failed.\n");
      return 1;
    }
  }

  // -------------------------------------------------------
  // Convert to RGB565 and write hex file
  // Each line: 4 uppercase hex digits = one 16-bit pixel
  // Total lines: 320 * 240 = 76800
  // -------------------------------------------------------
  FILE *out = fopen(argv[2], "w");
  if (!out) {
    perror("Cannot open output file");
    return 1;
  }

  for (int y = 0; y < OUT_HEIGHT; y++) {
    for (int x = 0; x < OUT_WIDTH; x++) {
      Pixel p = out_pixels[y * OUT_WIDTH + x];
      uint16_t rgb565 = to_rgb565(p.r, p.g, p.b);
      fprintf(out, "%04X\n", rgb565);
    }
  }

  fclose(out);
  free(out_pixels);

  printf("Done. Wrote %d pixels to %s\n", OUT_WIDTH * OUT_HEIGHT, argv[2]);
  printf("Load in Verilog with: $readmemh(\"%s\", frame_buffer);\n", argv[2]);
  return 0;
}
