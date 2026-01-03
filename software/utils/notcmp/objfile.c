/*
 * Support functions for outputting different object file formats
 * 
 *  Copyright (C) 2025 Eduardo Casino
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, Version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "objfile.h"

/* Format-specific constants */
#define PAP_BYTES_PER_LINE   24
#define INTEL_BYTES_PER_LINE 32
#define MAX_BYTES_PER_LINE   ((PAP_BYTES_PER_LINE > INTEL_BYTES_PER_LINE) ? \
                              PAP_BYTES_PER_LINE : INTEL_BYTES_PER_LINE)

/* Record format strings */
#define PAP_RECORD_PREFIX    ";%02X%04X"
#define PAP_RECORD_SUFFIX    "%04X\n"
#define PAP_TRAILER_FORMAT   ";00%04X%04X\n"
#define INTEL_RECORD_PREFIX  ":%02X%04X00"
#define INTEL_RECORD_SUFFIX  "%02X\n"
#define INTEL_EOF_RECORD     ":00000001FF\n"

/*
 * Calculate checksum for a line of data.
 * For both formats, checksum includes: byte_count + address_high + address_low + data_bytes
 */
static uint16_t calculate_checksum(uint16_t addr, const uint8_t *data, uint8_t data_len)
{
    uint16_t checksum = data_len + ((addr >> 8) & 0xFF) + (addr & 0xFF);
    
    for (uint8_t i = 0; i < data_len; ++i) {
        checksum += data[i];
    }
    
    return checksum;
}

/*
 * Write hex-encoded data bytes to file.
 */
static int write_hex_bytes(FILE *file, const uint8_t *data, uint8_t count)
{
    for (uint8_t i = 0; i < count; ++i) {
        if (fprintf(file, "%02X", data[i]) < 0) {
            return -1;
        }
    }
    return 0;
}

/*
 * Write a single PAP format record.
 * Format: ;LLAAAADDDD...DDDDCCCC
 * Where: LL=length, AAAA=address, DD=data bytes, CCCC=checksum
 */
static int write_pap_record(FILE *file, uint16_t addr, const uint8_t *data, 
                            uint8_t data_len, uint16_t checksum)
{
    if (fprintf(file, PAP_RECORD_PREFIX, data_len, addr) < 0) {
        return -1;
    }
    
    if (write_hex_bytes(file, data, data_len) < 0) {
        return -1;
    }
    
    if (fprintf(file, PAP_RECORD_SUFFIX, checksum) < 0) {
        return -1;
    }
    
    return 0;
}

/*
 * Write a single Intel HEX format record.
 * Format: :LLAAAATTDDDD...DDDDCC
 * Where: LL=length, AAAA=address, TT=type(00), DD=data, CC=checksum (two's complement)
 */
static int write_intel_record(FILE *file, uint16_t addr, const uint8_t *data, 
                              uint8_t data_len, uint16_t checksum)
{
    if (fprintf(file, INTEL_RECORD_PREFIX, data_len, addr) < 0) {
        return -1;
    }
    
    if (write_hex_bytes(file, data, data_len) < 0) {
        return -1;
    }
    
    /* Intel HEX uses two's complement of checksum */
    uint8_t intel_checksum = (uint8_t)(~checksum + 1);
    if (fprintf(file, INTEL_RECORD_SUFFIX, intel_checksum) < 0) {
        return -1;
    }
    
    return 0;
}

/*
 * Write PAP format trailer record.
 * Format: ;00LLLLCCCC where LLLL=line count, CCCC=checksum of line count
 */
static int write_pap_trailer(FILE *file, uint16_t line_count)
{
    uint16_t trailer_checksum = ((line_count >> 8) & 0xFF) + (line_count & 0xFF);
    return fprintf(file, PAP_TRAILER_FORMAT, line_count, trailer_checksum) < 0 ? -1 : 0;
}

/*
 * Write Intel HEX EOF record.
 */
static int write_intel_eof(FILE *file)
{
    return fputs(INTEL_EOF_RECORD, file) < 0 ? -1 : 0;
}

/*
 * Write data in binary format (raw bytes).
 */
static int write_binary_format(FILE *file, const uint8_t *data, size_t size)
{
    return (fwrite(data, 1, size, file) != size) ? -1 : 0;
}

/*
 * Write data in hex format (PAP or Intel HEX).
 * Breaks data into lines and writes records with checksums.
 */
static int write_hex_format(FILE *file, const uint8_t *data, size_t size, 
                            uint16_t base_addr, bool is_pap)
{
    const uint8_t bytes_per_line = is_pap ? PAP_BYTES_PER_LINE : INTEL_BYTES_PER_LINE;
    uint8_t line_buffer[MAX_BYTES_PER_LINE];
    size_t bytes_remaining = size;
    uint16_t current_addr = base_addr;
    uint16_t line_count = 0;
    
    while (bytes_remaining > 0) {
        /* Determine bytes for this line */
        const uint8_t bytes_this_line = (bytes_remaining > bytes_per_line) 
                                        ? bytes_per_line 
                                        : (uint8_t)bytes_remaining;
        
        /* Copy data to line buffer */
        memcpy(line_buffer, data, bytes_this_line);
        
        /* Calculate checksum */
        const uint16_t checksum = calculate_checksum(current_addr, line_buffer, 
                                                     bytes_this_line);
        
        /* Write the record */
        const int write_result = is_pap 
            ? write_pap_record(file, current_addr, line_buffer, bytes_this_line, checksum)
            : write_intel_record(file, current_addr, line_buffer, bytes_this_line, checksum);
            
        if (write_result < 0) {
            return -1;
        }
        
        /* Advance to next line */
        data += bytes_this_line;
        bytes_remaining -= bytes_this_line;
        current_addr += bytes_this_line;
        ++line_count;
    }
    
    /* Write format-specific trailer */
    return is_pap ? write_pap_trailer(file, line_count) : write_intel_eof(file);
}

int objfile_write(output_format_t format, FILE *file, uint8_t *data, 
                  size_t size, uint16_t base_addr)
{
    if (!file || !data) {
        return -1;
    }
    
    if (size == 0) {
        return 0;
    }
    
    switch (format) {
        case OUT_BIN:
            return write_binary_format(file, data, size);
            
        case OUT_PAP:
            return write_hex_format(file, data, size, base_addr, true);
            
        case OUT_IHEX:
            return write_hex_format(file, data, size, base_addr, false);
            
        default:
            return -1;
    }
}