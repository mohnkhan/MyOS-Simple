// ============================================================================
//  rtc.h  —  CMOS RTC register map and clock API
//
//  CMOS/RTC port numbers (0x70 / 0x71), the register addresses for each time
//  field, status-register flags (24-hour / binary-vs-BCD), the rtc_time_t
//  struct, and the function prototypes implemented in rtc.c.
// ============================================================================
#ifndef RTC_H
#define RTC_H

#include "stdint.h"

// CMOS/RTC I/O ports
#define CMOS_ADDRESS    0x70
#define CMOS_DATA       0x71

// CMOS RTC register addresses
#define RTC_SECONDS     0x00
#define RTC_MINUTES     0x02
#define RTC_HOURS       0x04
#define RTC_WEEKDAY     0x06
#define RTC_DAY         0x07
#define RTC_MONTH       0x08
#define RTC_YEAR        0x09
#define RTC_CENTURY     0x32    // Century register (if available)
#define RTC_STATUS_A    0x0A
#define RTC_STATUS_B    0x0B

// RTC Status Register B flags
#define RTC_24HOUR      0x02    // 24-hour mode flag
#define RTC_BINARY      0x04    // Binary mode flag (vs BCD)

// Time structure
typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t weekday;
} rtc_time_t;

// RTC functions
void rtc_init(void);
void rtc_read_time(rtc_time_t* time);
uint8_t rtc_read_register(uint8_t reg);
void rtc_write_register(uint8_t reg, uint8_t value);
uint8_t bcd_to_binary(uint8_t bcd);
void rtc_get_time_string(char* buffer);
void rtc_get_date_string(char* buffer);

// Uptime tracking functions
void rtc_record_boot_time(void);
void rtc_get_uptime_string(char* buffer);
uint32_t rtc_get_uptime_seconds(void);

#endif // RTC_H
