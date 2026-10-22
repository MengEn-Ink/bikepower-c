#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ble_payload_codec.h"

static bool parse_long(const char *text, long min_value, long max_value, long *out)
{
    if (!text || !out) return false;
    errno = 0;
    char *end = NULL;
    long value = strtol(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || value < min_value || value > max_value) {
        return false;
    }
    *out = value;
    return true;
}

static bool parse_u32(const char *text, uint32_t *out)
{
    if (!text || !out || text[0] == '-') return false;
    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) return false;
    *out = (uint32_t)value;
    return true;
}

static void print_hex(const uint8_t *payload, size_t length)
{
    for (size_t i = 0; i < length; i++) printf("%02x", payload[i]);
    putchar('\n');
}

static int usage(const char *program)
{
    fprintf(stderr,
            "usage:\n"
            "  %s cps POWER REV TIME1024\n"
            "  %s csc REV TIME1024\n"
            "  %s hrs BPM\n"
            "  %s ftms_bike CADENCE_ON POWER_ON SPEED_X100 CADENCE_HALF_RPM POWER\n"
            "  %s ftms_treadmill SPEED_X100 DISTANCE_M BPM ELAPSED_S\n"
            "  %s rsc RUNNING SPEED_1_256 CADENCE STRIDE_CM DISTANCE_DM\n",
            program, program, program, program, program, program);
    return 2;
}

int main(int argc, char **argv)
{
    uint8_t payload[BLE_PAYLOAD_FTMS_BIKE_MAX_LEN > BLE_PAYLOAD_RSC_LEN
                    ? BLE_PAYLOAD_FTMS_BIKE_MAX_LEN : BLE_PAYLOAD_RSC_LEN];
    size_t length = 0;
    long values[6] = { 0 };
    uint32_t distance_dm = 0;

    if (argc < 2) return usage(argv[0]);
    if (strcmp(argv[1], "cps") == 0) {
        if (argc != 5
                || !parse_long(argv[2], INT16_MIN, INT16_MAX, &values[0])
                || !parse_long(argv[3], 0, UINT16_MAX, &values[1])
                || !parse_long(argv[4], 0, UINT16_MAX, &values[2])) {
            return usage(argv[0]);
        }
        length = ble_payload_encode_cps(payload, sizeof(payload),
                                        (int16_t)values[0], (uint16_t)values[1], (uint16_t)values[2]);
    } else if (strcmp(argv[1], "csc") == 0) {
        if (argc != 4
                || !parse_long(argv[2], 0, UINT16_MAX, &values[0])
                || !parse_long(argv[3], 0, UINT16_MAX, &values[1])) {
            return usage(argv[0]);
        }
        length = ble_payload_encode_csc(payload, sizeof(payload),
                                        (uint16_t)values[0], (uint16_t)values[1]);
    } else if (strcmp(argv[1], "hrs") == 0) {
        if (argc != 3 || !parse_long(argv[2], 0, UINT8_MAX, &values[0])) return usage(argv[0]);
        length = ble_payload_encode_hrs(payload, sizeof(payload), (uint8_t)values[0]);
    } else if (strcmp(argv[1], "ftms_bike") == 0) {
        if (argc != 7
                || !parse_long(argv[2], 0, 1, &values[0])
                || !parse_long(argv[3], 0, 1, &values[1])
                || !parse_long(argv[4], 0, UINT16_MAX, &values[2])
                || !parse_long(argv[5], 0, UINT16_MAX, &values[3])
                || !parse_long(argv[6], INT16_MIN, INT16_MAX, &values[4])) {
            return usage(argv[0]);
        }
        length = ble_payload_encode_ftms_bike(payload, sizeof(payload),
                                              values[0] != 0, values[1] != 0,
                                              (uint16_t)values[2], (uint16_t)values[3],
                                              (int16_t)values[4]);
    } else if (strcmp(argv[1], "ftms_treadmill") == 0) {
        if (argc != 6
                || !parse_long(argv[2], 0, UINT16_MAX, &values[0])
                || !parse_long(argv[3], 0, 0x00ffffffL, &values[1])
                || !parse_long(argv[4], 0, UINT8_MAX, &values[2])
                || !parse_long(argv[5], 0, UINT16_MAX, &values[3])) {
            return usage(argv[0]);
        }
        length = ble_payload_encode_ftms_treadmill(payload, sizeof(payload),
                                                   (uint16_t)values[0], (uint32_t)values[1],
                                                   (uint8_t)values[2], (uint16_t)values[3]);
    } else if (strcmp(argv[1], "rsc") == 0) {
        if (argc != 7
                || !parse_long(argv[2], 0, 1, &values[0])
                || !parse_long(argv[3], 0, UINT16_MAX, &values[1])
                || !parse_long(argv[4], 0, UINT8_MAX, &values[2])
                || !parse_long(argv[5], 0, UINT16_MAX, &values[3])
                || !parse_u32(argv[6], &distance_dm)) {
            return usage(argv[0]);
        }
        length = ble_payload_encode_rsc(payload, sizeof(payload), values[0] != 0,
                                        (uint16_t)values[1], (uint8_t)values[2],
                                        (uint16_t)values[3], distance_dm);
    } else {
        return usage(argv[0]);
    }

    if (length == 0) {
        fprintf(stderr, "payload encoding failed\n");
        return 1;
    }
    print_hex(payload, length);
    return 0;
}
