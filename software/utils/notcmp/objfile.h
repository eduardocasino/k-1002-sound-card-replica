#ifndef OBJFILE_H
#define OBJFILE_H
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

typedef enum { 
    OUT_BIN = 0,  /* Binary format (raw bytes) */
    OUT_PAP,      /* PAP hex format */
    OUT_IHEX      /* Intel HEX format */
} output_format_t;

/**
 * Write data to file in the specified object file format.
 * 
 * @param format Output format (binary, PAP, or Intel HEX)
 * @param file Output file handle (must be writable)
 * @param data Source data buffer
 * @param size Number of bytes to write
 * @param base_addr Starting address for hex formats (ignored for binary)
 * @return 0 on success, -1 on error
 */
int objfile_write(output_format_t format, FILE *file, uint8_t *data, 
                  size_t size, uint16_t base_addr);

#endif /* OBJFILE_H */