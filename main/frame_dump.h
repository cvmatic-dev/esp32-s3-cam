#pragma once
#include <stddef.h>
#include <stdint.h>

/*
 * Emit a binary blob on the console so the host can reconstruct it:
 *
 *   @@IMG_BEGIN name=<name> len=<bytes> crc32=<hex>
 *   @@IMG:<base64 line, 76 chars max>
 *   ...
 *   @@IMG_END name=<name>
 *
 * crc32 is the standard (zlib/IEEE) CRC-32 of the raw bytes.
 * Decoded by tools/esp_monitor.py --save-images DIR.
 */
void frame_dump(const char *name, const uint8_t *data, size_t len);
