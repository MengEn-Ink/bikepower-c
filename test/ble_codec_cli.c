#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ble_payload_codec.h"

static void print_hex(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        printf("%02x", buf[i]);
    }
    putchar('\n');
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;

    uint8_t buf[16] = { 0 };
    size_t len = 0;
    if (strcmp(argv[1], "cps") == 0 && argc == 5) {
        len = ble_payload_encode_cps(buf, sizeof(buf),
                                     (int16_t)strtol(argv[2], NULL, 10),
                                     (uint16_t)strtoul(argv[3], NULL, 10),
                                     (uint16_t)strtoul(argv[4], NULL, 10));
    } else if (strcmp(argv[1], "csc") == 0 && argc == 4) {
        len = ble_payload_encode_csc(buf, sizeof(buf),
                                     (uint16_t)strtoul(argv[2], NULL, 10),
                                     (uint16_t)strtoul(argv[3], NULL, 10));
    } else if (strcmp(argv[1], "hrs") == 0 && argc == 3) {
        len = ble_payload_encode_hrs(buf, sizeof(buf),
                                     (uint8_t)strtoul(argv[2], NULL, 10));
    } else if (strcmp(argv[1], "ftms_bike") == 0 && argc == 7) {
        len = ble_payload_encode_indoor_bike(
            buf, sizeof(buf),
            strtol(argv[2], NULL, 10) != 0,
            strtol(argv[3], NULL, 10) != 0,
            (uint16_t)strtoul(argv[4], NULL, 10),
            (uint16_t)strtoul(argv[5], NULL, 10),
            (int16_t)strtol(argv[6], NULL, 10));
    } else if (strcmp(argv[1], "ftms_treadmill") == 0 && argc == 6) {
        len = ble_payload_encode_treadmill(
            buf, sizeof(buf),
            (uint16_t)strtoul(argv[2], NULL, 10),
            (uint32_t)strtoul(argv[3], NULL, 10),
            (uint8_t)strtoul(argv[4], NULL, 10),
            (uint16_t)strtoul(argv[5], NULL, 10));
    } else if (strcmp(argv[1], "rsc") == 0 && argc == 7) {
        len = ble_payload_encode_rsc(
            buf, sizeof(buf),
            strtol(argv[2], NULL, 10) != 0,
            (uint16_t)strtoul(argv[3], NULL, 10),
            (uint8_t)strtoul(argv[4], NULL, 10),
            (uint16_t)strtoul(argv[5], NULL, 10),
            (uint32_t)strtoul(argv[6], NULL, 10));
    } else {
        return 2;
    }

    if (len == 0) return 3;
    print_hex(buf, len);
    return 0;
}
