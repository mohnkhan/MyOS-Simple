// ============================================================================
//  rtc.c  —  CMOS real-time clock and uptime tracking
//
//  Reads wall-clock time and date from the CMOS RTC through I/O ports 0x70
//  (register select) and 0x71 (data), waiting out the update-in-progress flag
//  and converting BCD to binary. Formats time/date strings, records a boot
//  timestamp, and derives uptime from the difference between now and boot.
//  inb/outb are inline-asm wrappers; there is no libc.
// ============================================================================
#include "rtc.h"

// Global variable to store boot time
static rtc_time_t boot_time;
static uint8_t boot_time_recorded = 0;

// I/O port functions
static inline unsigned char inb(unsigned short port) {
    unsigned char result;
    __asm__ __volatile__("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outb(unsigned short port, unsigned char data) {
    __asm__ __volatile__("outb %0, %1" : : "a"(data), "Nd"(port));
}

// Convert BCD to binary
uint8_t bcd_to_binary(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

// Read a register from CMOS
uint8_t rtc_read_register(uint8_t reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

// Write a register to CMOS
void rtc_write_register(uint8_t reg, uint8_t value) {
    outb(CMOS_ADDRESS, reg);
    outb(CMOS_DATA, value);
}

// Wait for RTC update to complete
static void rtc_wait_update(void) {
    // Wait for any update in progress to finish
    while (rtc_read_register(RTC_STATUS_A) & 0x80);
}

// Initialize RTC
void rtc_init(void) {
    uint8_t status_b = rtc_read_register(RTC_STATUS_B);
    
    // Enable 24-hour mode if not already enabled
    if (!(status_b & RTC_24HOUR)) {
        rtc_write_register(RTC_STATUS_B, status_b | RTC_24HOUR);
    }
}

// Read current time from RTC
void rtc_read_time(rtc_time_t* time) {
    uint8_t status_b;
    uint8_t century = 20;  // Default to 21st century
    
    // Wait for any update in progress
    rtc_wait_update();
    
    // Read time registers
    time->second = rtc_read_register(RTC_SECONDS);
    time->minute = rtc_read_register(RTC_MINUTES);
    time->hour = rtc_read_register(RTC_HOURS);
    time->day = rtc_read_register(RTC_DAY);
    time->month = rtc_read_register(RTC_MONTH);
    time->year = rtc_read_register(RTC_YEAR);
    time->weekday = rtc_read_register(RTC_WEEKDAY);
    
    // Check if values are in BCD or binary mode
    status_b = rtc_read_register(RTC_STATUS_B);
    
    if (!(status_b & RTC_BINARY)) {
        // Convert from BCD to binary
        time->second = bcd_to_binary(time->second);
        time->minute = bcd_to_binary(time->minute);
        time->hour = bcd_to_binary(time->hour);
        time->day = bcd_to_binary(time->day);
        time->month = bcd_to_binary(time->month);
        time->year = bcd_to_binary(time->year);
        time->weekday = bcd_to_binary(time->weekday);
    }
    
    // Handle 12-hour format if necessary
    if (!(status_b & RTC_24HOUR)) {
        // Convert 12-hour to 24-hour format
        uint8_t pm = time->hour & 0x80;
        time->hour &= 0x7F;
        if (pm && time->hour != 12) {
            time->hour += 12;
        } else if (!pm && time->hour == 12) {
            time->hour = 0;
        }
    }
    
    // Calculate full year (RTC typically stores only last 2 digits)
    time->year += (century * 100);
}

// Helper function to convert number to string with padding
static void num_to_str_padded(uint8_t num, char* str, int padding) {
    if (padding == 2 && num < 10) {
        str[0] = '0';
        str[1] = num + '0';
        str[2] = '\0';
    } else if (num < 10) {
        str[0] = num + '0';
        str[1] = '\0';
    } else {
        str[0] = (num / 10) + '0';
        str[1] = (num % 10) + '0';
        str[2] = '\0';
    }
}

// Helper function for 4-digit year
static void year_to_str(uint16_t year, char* str) {
    str[0] = (year / 1000) + '0';
    str[1] = ((year / 100) % 10) + '0';
    str[2] = ((year / 10) % 10) + '0';
    str[3] = (year % 10) + '0';
    str[4] = '\0';
}

// Get time as formatted string (HH:MM:SS)
void rtc_get_time_string(char* buffer) {
    rtc_time_t time;
    char temp[5];
    int pos = 0;
    
    rtc_read_time(&time);
    
    // Format: HH:MM:SS
    num_to_str_padded(time.hour, temp, 2);
    buffer[pos++] = temp[0];
    buffer[pos++] = temp[1];
    buffer[pos++] = ':';
    
    num_to_str_padded(time.minute, temp, 2);
    buffer[pos++] = temp[0];
    buffer[pos++] = temp[1];
    buffer[pos++] = ':';
    
    num_to_str_padded(time.second, temp, 2);
    buffer[pos++] = temp[0];
    buffer[pos++] = temp[1];
    
    buffer[pos] = '\0';
}

// Get date as formatted string (DD/MM/YYYY)
void rtc_get_date_string(char* buffer) {
    rtc_time_t time;
    char temp[5];
    int pos = 0;
    
    rtc_read_time(&time);
    
    // Format: DD/MM/YYYY
    num_to_str_padded(time.day, temp, 2);
    buffer[pos++] = temp[0];
    buffer[pos++] = temp[1];
    buffer[pos++] = '/';
    
    num_to_str_padded(time.month, temp, 2);
    buffer[pos++] = temp[0];
    buffer[pos++] = temp[1];
    buffer[pos++] = '/';
    
    year_to_str(time.year, temp);
    buffer[pos++] = temp[0];
    buffer[pos++] = temp[1];
    buffer[pos++] = temp[2];
    buffer[pos++] = temp[3];
    
    buffer[pos] = '\0';
}

// Get weekday name
const char* get_weekday_name(uint8_t weekday) {
    switch(weekday) {
        case 1: return "Sunday";
        case 2: return "Monday";
        case 3: return "Tuesday";
        case 4: return "Wednesday";
        case 5: return "Thursday";
        case 6: return "Friday";
        case 7: return "Saturday";
        default: return "Unknown";
    }
}

// Get month name
const char* get_month_name(uint8_t month) {
    switch(month) {
        case 1: return "January";
        case 2: return "February";
        case 3: return "March";
        case 4: return "April";
        case 5: return "May";
        case 6: return "June";
        case 7: return "July";
        case 8: return "August";
        case 9: return "September";
        case 10: return "October";
        case 11: return "November";
        case 12: return "December";
        default: return "Unknown";
    }
}

// Record the boot time (should be called once at system startup)
void rtc_record_boot_time(void) {
    rtc_read_time(&boot_time);
    boot_time_recorded = 1;
}

// Convert time to seconds since midnight
static uint32_t time_to_seconds(rtc_time_t* time) {
    return (uint32_t)time->hour * 3600 + 
           (uint32_t)time->minute * 60 + 
           (uint32_t)time->second;
}

// Calculate days in month
static uint8_t days_in_month(uint8_t month, uint16_t year) {
    if (month == 2) {
        // Check for leap year
        if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
            return 29;
        }
        return 28;
    }
    if (month == 4 || month == 6 || month == 9 || month == 11) {
        return 30;
    }
    return 31;
}

// Calculate total seconds between two times
static uint32_t calculate_time_diff(rtc_time_t* start, rtc_time_t* end) {
    uint32_t diff_seconds = 0;
    
    // If same day
    if (start->year == end->year && start->month == end->month && start->day == end->day) {
        uint32_t start_sec = time_to_seconds(start);
        uint32_t end_sec = time_to_seconds(end);
        if (end_sec >= start_sec) {
            diff_seconds = end_sec - start_sec;
        } else {
            // Clock wrapped around midnight
            diff_seconds = (86400 - start_sec) + end_sec;
        }
    } else {
        // Different days - calculate day difference
        uint32_t days = 0;
        
        // Simple calculation for same month
        if (start->year == end->year && start->month == end->month) {
            days = end->day - start->day;
        } else {
            // More complex calculation for different months/years
            // For simplicity, assume maximum 30 days uptime
            days = 1; // At least one day difference
            
            // Add remaining time from start day
            diff_seconds = 86400 - time_to_seconds(start);
            // Add elapsed time in end day
            diff_seconds += time_to_seconds(end);
            // Add full days in between
            if (days > 1) {
                diff_seconds += (days - 1) * 86400;
            }
            return diff_seconds;
        }
        
        // Calculate seconds
        diff_seconds = days * 86400;
        uint32_t start_sec = time_to_seconds(start);
        uint32_t end_sec = time_to_seconds(end);
        diff_seconds -= start_sec;
        diff_seconds += end_sec;
    }
    
    return diff_seconds;
}

// Get system uptime in seconds
uint32_t rtc_get_uptime_seconds(void) {
    if (!boot_time_recorded) {
        return 0;
    }
    
    rtc_time_t current_time;
    rtc_read_time(&current_time);
    
    return calculate_time_diff(&boot_time, &current_time);
}

// Format uptime as string (DD days, HH:MM:SS)
void rtc_get_uptime_string(char* buffer) {
    if (!boot_time_recorded) {
        // Copy string manually
        const char* msg = "Uptime not available";
        int i = 0;
        while (msg[i]) {
            buffer[i] = msg[i];
            i++;
        }
        buffer[i] = '\0';
        return;
    }
    
    uint32_t uptime = rtc_get_uptime_seconds();
    uint32_t days = uptime / 86400;
    uint32_t hours = (uptime % 86400) / 3600;
    uint32_t minutes = (uptime % 3600) / 60;
    uint32_t seconds = uptime % 60;
    
    int pos = 0;
    char temp[12];
    
    // Format days if any
    if (days > 0) {
        // Convert days to string
        if (days >= 10) {
            temp[0] = (days / 10) + '0';
            temp[1] = (days % 10) + '0';
            temp[2] = '\0';
            buffer[pos++] = temp[0];
            buffer[pos++] = temp[1];
        } else {
            buffer[pos++] = days + '0';
        }
        
        // Add " day" or " days"
        buffer[pos++] = ' ';
        buffer[pos++] = 'd';
        buffer[pos++] = 'a';
        buffer[pos++] = 'y';
        if (days != 1) {
            buffer[pos++] = 's';
        }
        buffer[pos++] = ',';
        buffer[pos++] = ' ';
    }
    
    // Format HH:MM:SS
    num_to_str_padded(hours, temp, 2);
    buffer[pos++] = temp[0];
    buffer[pos++] = temp[1];
    buffer[pos++] = ':';
    
    num_to_str_padded(minutes, temp, 2);
    buffer[pos++] = temp[0];
    buffer[pos++] = temp[1];
    buffer[pos++] = ':';
    
    num_to_str_padded(seconds, temp, 2);
    buffer[pos++] = temp[0];
    buffer[pos++] = temp[1];
    
    buffer[pos] = '\0';
}
