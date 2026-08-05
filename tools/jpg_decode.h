#ifndef TOYC_JPG_DECODE_H
#define TOYC_JPG_DECODE_H

#include <stddef.h>
#include <stdint.h>

/* Decode a baseline JPEG (SOF0, 8-bit, grayscale or YCbCr 4:4:4/4:2:2/4:2:0)
 * into packed RGB888 pixels. On success returns 0 and stores a malloc'd
 * w*h*3 buffer in *pix (caller frees); on any failure returns -1 and sets
 * *pix to NULL. Progressive, 12-bit and other JPEG variants are rejected. */
int jpg_to_rgb(const unsigned char *in, size_t n,
               unsigned char **pix, uint32_t *w, uint32_t *h);

#endif
