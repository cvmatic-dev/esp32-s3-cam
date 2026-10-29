#include "frame_dump.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "esp_rom_crc.h"

#define RAW_PER_LINE 57 /* -> 76 base64 chars */

/* A full frame is tens of KB; the blocking console write takes seconds and
 * would otherwise starve the idle task and trip the task watchdog. */
#define YIELD_EVERY_LINES 32

void frame_dump(const char *name, const uint8_t *data, size_t len)
{
    /* esp_rom_crc32_le(0, ...) == zlib.crc32(...) */
    uint32_t crc = esp_rom_crc32_le(0, data, len);
    printf("\n@@IMG_BEGIN name=%s len=%u crc32=%08lx\n", name, (unsigned)len, (unsigned long)crc);

    unsigned char line[80];
    int emitted = 0;
    for (size_t off = 0; off < len; off += RAW_PER_LINE) {
        size_t chunk = (len - off) < RAW_PER_LINE ? (len - off) : RAW_PER_LINE;
        size_t olen = 0;
        if (mbedtls_base64_encode(line, sizeof(line) - 1, &olen, data + off, chunk) != 0) {
            printf("@@IMG_ABORT name=%s reason=b64\n", name);
            return;
        }
        line[olen] = '\0';
        printf("@@IMG:%s\n", line);
        if (++emitted % YIELD_EVERY_LINES == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    printf("@@IMG_END name=%s\n", name);
    fflush(stdout);
}
