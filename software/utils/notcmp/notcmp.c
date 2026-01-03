/*
 * NOTRAN compiler - A compiler for the NOTRAN music notation language
 *
 * Based on the original 6502 assembly implementation by Hal Chamberlin
 *
 * Copyright (C) 2025 Eduardo Casino
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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <getopt.h>
#include "objfile.h"

/* ============================================================================
 * Constants and Configuration
 * ============================================================================ */

#define MAX_LINE_LENGTH 256
#define MAX_SYMBOLS 100
#define MAX_CODE_SIZE 8192
#define NUM_VOICES 4

#define INACTIVE_VOICE_DURATION 0xFF
#define ACTIVE_VOICE_DURATION 0

#define MIN_OCTAVE 1
#define MAX_OCTAVE 6
#define MIN_PITCH 1
#define MAX_PITCH 61

#define MIN_WAVEFORM 1
#define MAX_WAVEFORM 16

#define MIN_TEMPO 1
#define MAX_TEMPO 255

/* Opcodes */
#define OP_END 0x00
#define OP_TEMPO 0x10
#define OP_JSR 0x20
#define OP_RTS 0x30
#define OP_JMP 0x40
#define OP_SET_VOICES 0x50
#define OP_LONG_NOTE 0x60
#define OP_REST_MASK 0x80
#define OP_VOICE_DEACTIVATE 0x80
#define OP_VOICE_ACTIVATE 0x90

/* Error codes */
typedef enum {
    ERR_NONE = 0,
    ERR_ARG_OUT_OF_RANGE,
    ERR_UNDEFINED_IDENTIFIER,
    ERR_DUPLICATE_IDENTIFIER,
    ERR_SYMBOL_TABLE_OVERFLOW,
    ERR_CODE_OVERFLOW,
    ERR_INCOMPREHENSIBLE_SPEC,
    ERR_VOICE_MISMATCH,
    ERR_PITCH_OUT_OF_RANGE,
    ERR_ILLEGAL_DURATION,
    ERR_EXEC_CTRL_IN_EVENT,
    ERR_IDENTIFIER_IN_EVENT,
    ERR_NESTED_SUB_ESB,
    ERR_ESB_WITHOUT_SUB,
    ERR_HANGING_SUB,
    ERR_NO_VOICES_ACTIVE
} error_code_t;

/* ============================================================================
 * Data Structures
 * ============================================================================ */

typedef struct {
    uint8_t id;
    uint16_t address;
} symbol_t;

typedef struct {
    uint8_t voice;
    uint8_t pitch;
    uint8_t octave;
    uint8_t duration_code;
    uint8_t duration_time;
} note_spec_t;

typedef struct {
    uint8_t waveform;      /* 0-15 (waveform 1 stored as 0) */
    uint8_t duration;      /* Time units remaining */
    uint8_t pitch;         /* Last absolute pitch */
    uint8_t octave;        /* Current octave */
    bool use_absolute;     /* Force absolute pitch encoding */
} voice_state_t;

typedef struct {
    FILE *input_file;
    FILE *output_file;
    FILE *listing_file;
    output_format_t output_format;
    
    uint16_t base_address;
    bool listing_enabled;
    
    char input_line[MAX_LINE_LENGTH];
    const char *input_ptr;
    int line_number;
    
    symbol_t symbols[MAX_SYMBOLS];
    int symbol_count;
    
    uint8_t code[MAX_CODE_SIZE];
    size_t code_size;
    size_t line_code_start;
    
    bool event_building;
    uint8_t voice_ptr;
    voice_state_t voices[NUM_VOICES];
    
    uint16_t sub_address;
    bool end_flag;
    bool error_flag;
} compiler_t;

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static void init_compiler(compiler_t *c);
static void process_file(compiler_t *c);
static void process_line(compiler_t *c);
static void parse_identifier(compiler_t *c);
static bool parse_keyword(compiler_t *c);
static void parse_note(compiler_t *c);
static void process_note_event(compiler_t *c, const note_spec_t *note);
static void skip_whitespace(compiler_t *c);
static int parse_numeric_arg(compiler_t *c);
static bool add_symbol(compiler_t *c, uint8_t id, uint16_t addr);
static bool find_symbol(const compiler_t *c, uint8_t id, uint16_t *addr);
static void emit_byte(compiler_t *c, uint8_t byte);
static void emit_word(compiler_t *c, uint16_t word);
static void report_error(compiler_t *c, error_code_t code);
static const char* get_error_message(error_code_t code);
static void write_listing_line(compiler_t *c);

/* Keyword handlers */
static void handle_nvc(compiler_t *c);
static void handle_act(compiler_t *c);
static void handle_dct(compiler_t *c);
static void handle_voice_control(compiler_t *c, bool activate);
static void handle_wav(compiler_t *c);
static void handle_tpo(compiler_t *c);
static void handle_abs(compiler_t *c);
static void handle_jmp(compiler_t *c);
static void handle_jsr(compiler_t *c);
static void handle_jump(compiler_t *c, uint8_t opcode);
static void handle_rts(compiler_t *c);
static void handle_sub(compiler_t *c);
static void handle_esb(compiler_t *c);
static void handle_end(compiler_t *c);
static void check_event_conflict(compiler_t *c);

/* Helper functions */
static bool is_valid_voice(int voice_num);
static bool is_valid_waveform(int waveform);
static bool is_valid_pitch(int pitch);
static bool is_line_terminator(char ch);
static void activate_voice(compiler_t *c, int voice_idx);
static void deactivate_voice(compiler_t *c, int voice_idx);
static bool any_voice_active(const compiler_t *c);
static int find_next_voice_needing_note(const compiler_t *c, int start_idx);
static void complete_event(compiler_t *c);
static uint8_t calculate_min_voice_duration(const compiler_t *c);
static void subtract_duration_from_voices(compiler_t *c, uint8_t duration);

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int main(int argc, char *argv[]) {
    const char *input_file = NULL;
    const char *output_file = NULL;
    const char *listing_file = NULL;
    output_format_t out_fmt = OUT_BIN;
    uint16_t base_addr = 0;
    
    int opt;
    while ((opt = getopt(argc, argv, "o:l:a:f:")) != -1) {
        switch (opt) {
            case 'o': output_file = optarg; break;
            case 'l': listing_file = optarg; break;
            case 'a': base_addr = (uint16_t)strtoul(optarg, NULL, 0); break;
            case 'f':
                if (strcasecmp(optarg, "bin") == 0) out_fmt = OUT_BIN;
                else if (strcasecmp(optarg, "pap") == 0) out_fmt = OUT_PAP;
                else if (strcasecmp(optarg, "ihex") == 0) out_fmt = OUT_IHEX;
                else {
                    fprintf(stderr, "Unknown output format '%s' (expected: bin, pap, ihex)\n", optarg);
                    return EXIT_FAILURE;
                }            
                break;
            default:
                fprintf(stderr, "Usage: %s [-l listing.lst] -o output.bin -f {bin|pap|ihex} [-a address] input.not\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (optind < argc) {
        input_file = argv[optind++];
    }

    if (!input_file || !output_file) {
        fprintf(stderr, "Usage: %s [-l listing.lst] [-a address] [-f {bin|pap|ihex}] -o output.bin input.not\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    compiler_t c;
    init_compiler(&c);
    c.base_address = base_addr;
    c.output_format = out_fmt;
    c.listing_enabled = (listing_file != NULL);
    
    c.input_file = fopen(input_file, "r");
    if (!c.input_file) {
        perror("Cannot open input file");
        return EXIT_FAILURE;
    }
    
    if (listing_file) {
        c.listing_file = fopen(listing_file, "w");
        if (!c.listing_file) {
            perror("Cannot open listing file");
            fclose(c.input_file);
            return EXIT_FAILURE;
        }
    }
    
    process_file(&c);
    
    fclose(c.input_file);
    if (c.listing_file) {
        fclose(c.listing_file);
    }

    if (c.error_flag) {
        fprintf(stderr, "\nCompilation failed with errors.\n");
        return EXIT_FAILURE;
    }

    c.output_file = fopen(output_file, "wb");
    if (!c.output_file) {
        perror("Cannot open output file");
        return EXIT_FAILURE;
    }
    
    objfile_write(c.output_format, c.output_file, c.code, c.code_size, c.base_address);
    fclose(c.output_file);
    
    printf("Compilation successful:\n");
    printf("  Lines: %d\n", c.line_number);
    printf("  Code size: %zu bytes\n", c.code_size);
    printf("  Symbols: %d\n", c.symbol_count);
    printf("  Base address: 0x%04X\n", c.base_address);
    
    return EXIT_SUCCESS;
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

static void init_compiler(compiler_t *c) {
    memset(c, 0, sizeof(compiler_t));
    
    for (int i = 0; i < NUM_VOICES; i++) {
        c->voices[i].waveform = 0;
        c->voices[i].duration = INACTIVE_VOICE_DURATION;
        c->voices[i].use_absolute = true;
        c->voices[i].pitch = 0;
        c->voices[i].octave = 0;
    }
}

/* ============================================================================
 * File Processing
 * ============================================================================ */

static void normalize_line(char *line) {
    /* Remove trailing newlines/carriage returns */
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
        line[--len] = '\0';
    }
    
    /* Convert to uppercase */
    for (char *p = line; *p; p++) {
        *p = (char)toupper((unsigned char)*p);
    }
}

static void process_file(compiler_t *c) {
    while (fgets(c->input_line, sizeof(c->input_line), c->input_file)) {
        c->line_number++;
        normalize_line(c->input_line);
        process_line(c);
        
        if (c->error_flag || c->end_flag) {
            break;
        }
    }
}

static bool is_comment_line(const char *line) {
    return *line == '*';
}

static bool is_empty_line(const char *line) {
    return *line == '\0';
}

static void process_line(compiler_t *c) {
    c->input_ptr = c->input_line;
    c->line_code_start = c->code_size;
    
    if (is_comment_line(c->input_line)) {
        write_listing_line(c);
        return;
    }
    
    /* Parse identifier if line starts with a digit */
    if (isdigit((unsigned char)*c->input_ptr)) {
        parse_identifier(c);
    } else if (*c->input_ptr != ' ' && !is_empty_line(c->input_line)) {
        report_error(c, ERR_INCOMPREHENSIBLE_SPEC);
        write_listing_line(c);
        return;
    }
    
    /* Parse specifications (keywords and notes) */
    while (*c->input_ptr && !is_line_terminator(*c->input_ptr)) {
        skip_whitespace(c);
        if (!*c->input_ptr || is_line_terminator(*c->input_ptr)) {
            break;
        }
        
        if (!parse_keyword(c)) {
            parse_note(c);
        }
        
        skip_whitespace(c);
        if (*c->input_ptr == ';') {
            c->input_ptr++;
        }
    }
    
    write_listing_line(c);
}

/* ============================================================================
 * Identifier Parsing
 * ============================================================================ */

static void parse_identifier(compiler_t *c) {
    if (c->event_building) {
        report_error(c, ERR_IDENTIFIER_IN_EVENT);
        return;
    }
    
    int id = parse_numeric_arg(c);
    if (id == 0) {
        report_error(c, ERR_INCOMPREHENSIBLE_SPEC);
        return;
    }
    
    uint16_t dummy;
    if (find_symbol(c, (uint8_t)id, &dummy)) {
        report_error(c, ERR_DUPLICATE_IDENTIFIER);
        return;
    }
    
    add_symbol(c, (uint8_t)id, c->base_address + c->code_size);
}

/* ============================================================================
 * Listing Output
 * ============================================================================ */

static void write_listing_line(compiler_t *c) {
    if (!c->listing_enabled) {
        return;
    }
    
    size_t bytes_generated = c->code_size - c->line_code_start;
    
    if (is_comment_line(c->input_line)) {
        fprintf(c->listing_file, "%s\n", c->input_line);
        return;
    }
    
    if (is_empty_line(c->input_line)) {
        fprintf(c->listing_file, "\n");
        return;
    }

    /* Output: source line, then address and hex bytes */
    fprintf(c->listing_file, "%s\n", c->input_line);
    fprintf(c->listing_file, "%04X  ", 
            (unsigned)(c->base_address + c->line_code_start));

    for (size_t i = 0; i < bytes_generated; i++) {
        fprintf(c->listing_file, "%02X ", c->code[c->line_code_start + i]);
    }

    fprintf(c->listing_file, "\n");
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

static void skip_whitespace(compiler_t *c) {
    while (*c->input_ptr == ' ' || *c->input_ptr == '\t') {
        c->input_ptr++;
    }
}

static bool is_line_terminator(char ch) {
    return ch == '\r' || ch == '\n';
}

static bool is_valid_voice(int voice_num) {
    return voice_num >= 1 && voice_num <= NUM_VOICES;
}

static bool is_valid_waveform(int waveform) {
    return waveform >= MIN_WAVEFORM && waveform <= MAX_WAVEFORM;
}

static bool is_valid_pitch(int pitch) {
    return pitch >= MIN_PITCH && pitch <= MAX_PITCH;
}

static int parse_numeric_arg(compiler_t *c) {
    skip_whitespace(c);
    
    if (!isdigit((unsigned char)*c->input_ptr)) {
        report_error(c, ERR_INCOMPREHENSIBLE_SPEC);
        return 0;
    }
    
    int val = 0;
    bool overflow = false;
    
    while (isdigit((unsigned char)*c->input_ptr)) {
        int digit = *c->input_ptr - '0';
        int new_val = val * 10 + digit;
        
        if (new_val > 255) {
            overflow = true;
        }
        val = new_val;
        c->input_ptr++;
    }
    
    if (overflow) {
        report_error(c, ERR_ARG_OUT_OF_RANGE);
        return 0;
    }
    
    return val;
}

/* ============================================================================
 * Symbol Table Management
 * ============================================================================ */

static bool add_symbol(compiler_t *c, uint8_t id, uint16_t addr) {
    if (c->symbol_count >= MAX_SYMBOLS) {
        report_error(c, ERR_SYMBOL_TABLE_OVERFLOW);
        return false;
    }
    
    c->symbols[c->symbol_count].id = id;
    c->symbols[c->symbol_count].address = addr;
    c->symbol_count++;
    return true;
}

static bool find_symbol(const compiler_t *c, uint8_t id, uint16_t *addr) {
    for (int i = 0; i < c->symbol_count; i++) {
        if (c->symbols[i].id == id) {
            if (addr) {
                *addr = c->symbols[i].address;
            }
            return true;
        }
    }
    return false;
}

/* ============================================================================
 * Code Emission
 * ============================================================================ */

static void emit_byte(compiler_t *c, uint8_t byte) {
    if (c->code_size >= MAX_CODE_SIZE) {
        report_error(c, ERR_CODE_OVERFLOW);
        return;
    }
    c->code[c->code_size++] = byte;
}

static void emit_word(compiler_t *c, uint16_t word) {
    emit_byte(c, word & 0xFF);
    emit_byte(c, (word >> 8) & 0xFF);
}

/* ============================================================================
 * Error Handling
 * ============================================================================ */

static void report_error(compiler_t *c, error_code_t code) {
    fprintf(stderr, "Error %d on line %d: %s\n", 
            code, c->line_number, get_error_message(code));
    c->error_flag = true;
}

static const char* get_error_message(error_code_t code) {
    static const char *messages[] = {
        [ERR_NONE] = "No error",
        [ERR_ARG_OUT_OF_RANGE] = "Argument out of range",
        [ERR_UNDEFINED_IDENTIFIER] = "Undefined identifier",
        [ERR_DUPLICATE_IDENTIFIER] = "Identifier already used",
        [ERR_SYMBOL_TABLE_OVERFLOW] = "Symbol table overflow",
        [ERR_CODE_OVERFLOW] = "Object code overflow",
        [ERR_INCOMPREHENSIBLE_SPEC] = "Incomprehensible specification",
        [ERR_VOICE_MISMATCH] = "Voice number mismatch",
        [ERR_PITCH_OUT_OF_RANGE] = "Note pitch out of range",
        [ERR_ILLEGAL_DURATION] = "Illegal duration",
        [ERR_EXEC_CTRL_IN_EVENT] = "Executable control in event",
        [ERR_IDENTIFIER_IN_EVENT] = "Identifier in event",
        [ERR_NESTED_SUB_ESB] = "Nested SUB-ESB",
        [ERR_ESB_WITHOUT_SUB] = "ESB without SUB",
        [ERR_HANGING_SUB] = "Hanging SUB",
        [ERR_NO_VOICES_ACTIVE] = "No voices active"
    };
    
    if (code >= 0 && code < sizeof(messages)/sizeof(messages[0]) && messages[code]) {
        return messages[code];
    }
    return "Unknown error";
}

/* ============================================================================
 * Note Parsing
 * ============================================================================ */

static uint8_t parse_note_pitch(compiler_t *c) {
    static const uint8_t pitch_table[] = {
        9,10,11, 11,12,1, 12,1,2, 2,3,4, 4,5,6, 5,6,7, 7,8,9
    };
    
    char note_letter = *c->input_ptr;
    if (note_letter < 'A' || note_letter > 'G') {
        report_error(c, ERR_INCOMPREHENSIBLE_SPEC);
        return 0;
    }
    
    int note_value = (note_letter - 'A' + 1) * 3;
    c->input_ptr++;
    
    /* Handle accidentals */
    if (*c->input_ptr == '#') {
        note_value++;
        c->input_ptr++;
    } else if (*c->input_ptr == '@') {
        note_value--;
        c->input_ptr++;
    }
    
    return pitch_table[note_value - 2];
}

static bool parse_duration(compiler_t *c, uint8_t *duration_code, uint8_t *duration_time) {
    static const char *duration_letters = "WHQEST";
    static const uint8_t code_table[] = {
        0,1,0, 2,3,5, 4,6,8, 7,9,11, 10,12,14, 13,15,0
    };
    static const uint8_t time_table[] = {
        192,144,96, 72,64,48, 36,32,24, 18,16,12, 9,8,6
    };
    
    const char *dur_pos = strchr(duration_letters, *c->input_ptr);
    if (!dur_pos) {
        report_error(c, ERR_ILLEGAL_DURATION);
        return false;
    }
    
    int dur_idx = (int)(dur_pos - duration_letters) * 3 + 1;
    c->input_ptr++;
    
    /* Handle dotted notes and triplets */
    if (*c->input_ptr == '.') {
        dur_idx--;
        c->input_ptr++;
    } else if (*c->input_ptr == '3') {
        dur_idx++;
        c->input_ptr++;
    }
    
    uint8_t code = code_table[dur_idx];
    if (code == 0) {
        report_error(c, ERR_ILLEGAL_DURATION);
        return false;
    }
    
    *duration_code = code;
    *duration_time = time_table[code - 1];
    return true;
}

static void parse_note(compiler_t *c) {
    note_spec_t note = {0};
    
    /* Optional voice digit */
    if (*c->input_ptr >= '1' && *c->input_ptr <= '0' + NUM_VOICES) {
        note.voice = *c->input_ptr - '0';
        c->input_ptr++;
    }
    
    /* Parse rest or note */
    if (*c->input_ptr == 'R') {
        c->input_ptr++;
        note.pitch = 0;  /* Rest */
    } else {
        note.pitch = parse_note_pitch(c);
        if (note.pitch == 0) {
            return;  /* Error already reported */
        }
        
        /* Optional octave */
        if (*c->input_ptr >= '1' && *c->input_ptr <= '6') {
            note.octave = *c->input_ptr - '0';
            c->input_ptr++;
        }
    }
    
    /* Parse duration */
    if (!parse_duration(c, &note.duration_code, &note.duration_time)) {
        return;  /* Error already reported */
    }
    
    /* Validate proper termination */
    if (*c->input_ptr != ' ' && *c->input_ptr != ';' && 
        *c->input_ptr != '\0' && !is_line_terminator(*c->input_ptr)) {
        report_error(c, ERR_INCOMPREHENSIBLE_SPEC);
        return;
    }
    
    process_note_event(c, &note);
}

/* ============================================================================
 * Voice State Management
 * ============================================================================ */

static void activate_voice(compiler_t *c, int voice_idx) {
    c->voices[voice_idx].duration = ACTIVE_VOICE_DURATION;
}

static void deactivate_voice(compiler_t *c, int voice_idx) {
    c->voices[voice_idx].duration = INACTIVE_VOICE_DURATION;
}

static bool any_voice_active(const compiler_t *c) {
    for (int i = 0; i < NUM_VOICES; i++) {
        if (c->voices[i].duration != INACTIVE_VOICE_DURATION) {
            return true;
        }
    }
    return false;
}

static int find_next_voice_needing_note(const compiler_t *c, int start_idx) {
    for (int i = start_idx; i < NUM_VOICES; i++) {
        if (c->voices[i].duration == 0) {
            return i;
        }
    }
    return NUM_VOICES;  /* Not found */
}

static uint8_t calculate_min_voice_duration(const compiler_t *c) {
    uint8_t min_duration = INACTIVE_VOICE_DURATION;
    
    for (int i = 0; i < NUM_VOICES; i++) {
        if (c->voices[i].duration != INACTIVE_VOICE_DURATION && 
            c->voices[i].duration < min_duration) {
            min_duration = c->voices[i].duration;
        }
    }
    
    return min_duration;
}

static void subtract_duration_from_voices(compiler_t *c, uint8_t duration) {
    for (int i = 0; i < NUM_VOICES; i++) {
        if (c->voices[i].duration != INACTIVE_VOICE_DURATION) {
            c->voices[i].duration -= duration;
        }
    }
}

static void complete_event(compiler_t *c) {
    uint8_t min_duration = calculate_min_voice_duration(c);
    subtract_duration_from_voices(c, min_duration);
    c->event_building = false;
}

/* ============================================================================
 * Note Event Processing
 * ============================================================================ */

static void emit_rest(compiler_t *c, uint8_t duration_code) {
    emit_byte(c, OP_REST_MASK | duration_code);
}

static void emit_short_note(compiler_t *c, int pitch_diff, uint8_t duration_code) {
    emit_byte(c, ((pitch_diff & 0x0F) << 4) | duration_code);
}

static void emit_long_note(compiler_t *c, int pitch, uint8_t waveform, uint8_t duration_code) {
    emit_byte(c, OP_LONG_NOTE);
    emit_byte(c, pitch * 2);
    emit_byte(c, (waveform << 4) | duration_code);
}

static bool should_use_short_encoding(const compiler_t *c, int voice_idx, int new_pitch) {
    if (c->voices[voice_idx].use_absolute || c->voices[voice_idx].pitch == 0) {
        return false;
    }
    
    int diff = new_pitch - c->voices[voice_idx].pitch;
    return diff >= -7 && diff <= 7;
}

static void process_note_event(compiler_t *c, const note_spec_t *note) {
    /* Start new event if needed */
    if (!c->event_building) {
        c->voice_ptr = 0;
        c->event_building = true;
        
        if (!any_voice_active(c)) {
            report_error(c, ERR_NO_VOICES_ACTIVE);
            exit(EXIT_FAILURE);
        }
    }
    
    /* Find the next voice that needs a note */
    int voice_idx = find_next_voice_needing_note(c, c->voice_ptr);
    
    if (voice_idx >= NUM_VOICES) {
        report_error(c, ERR_NO_VOICES_ACTIVE);
        return;
    }
    
    /* Check voice number match if specified */
    if (note->voice != 0 && voice_idx != note->voice - 1) {
        report_error(c, ERR_VOICE_MISMATCH);
    }
    
    /* Process rest */
    if (note->pitch == 0) {
        emit_rest(c, note->duration_code);
    } else {
        /* Process note */
        uint8_t octave = note->octave;
        if (octave == 0) {
            octave = c->voices[voice_idx].octave;
            if (octave == 0) {
                report_error(c, ERR_PITCH_OUT_OF_RANGE);
                octave = 4;
            }
        }
        c->voices[voice_idx].octave = octave;
        
        int absolute_pitch = octave * 12 + note->pitch - 12;
        if (!is_valid_pitch(absolute_pitch)) {
            report_error(c, ERR_PITCH_OUT_OF_RANGE);
            absolute_pitch = MAX_PITCH;
        }
        
        /* Choose short or long encoding */
        if (should_use_short_encoding(c, voice_idx, absolute_pitch)) {
            int pitch_diff = absolute_pitch - c->voices[voice_idx].pitch;
            emit_short_note(c, pitch_diff, note->duration_code);
        } else {
            emit_long_note(c, absolute_pitch, c->voices[voice_idx].waveform, note->duration_code);
        }
        
        c->voices[voice_idx].pitch = absolute_pitch;
    }

    /* Update voice state */
    c->voices[voice_idx].duration = note->duration_time;
    c->voices[voice_idx].use_absolute = false;

    /* Check if event is complete */
    int next_voice = voice_idx + 1;
    bool event_complete = (find_next_voice_needing_note(c, next_voice) >= NUM_VOICES);
    
    if (event_complete) {
        complete_event(c);
    } else {
        c->voice_ptr = next_voice;
    }
}

/* ============================================================================
 * Keyword Parsing and Handlers
 * ============================================================================ */

typedef struct {
    const char *keyword;
    void (*handler)(compiler_t *);
} keyword_handler_t;

static const keyword_handler_t keyword_table[] = {
    {"NVC", handle_nvc},
    {"ACT", handle_act},
    {"DCT", handle_dct},
    {"WAV", handle_wav},
    {"TPO", handle_tpo},
    {"ABS", handle_abs},
    {"JMP", handle_jmp},
    {"JSR", handle_jsr},
    {"RTS", handle_rts},
    {"SUB", handle_sub},
    {"ESB", handle_esb},
    {"END", handle_end},
    {NULL, NULL}
};

static bool parse_keyword(compiler_t *c) {
    skip_whitespace(c);
    
    if (!c->input_ptr[0] || !c->input_ptr[1] || !c->input_ptr[2]) {
        return false;
    }
    
    char keyword[4] = {c->input_ptr[0], c->input_ptr[1], c->input_ptr[2], '\0'};
    
    for (const keyword_handler_t *kw = keyword_table; kw->keyword != NULL; kw++) {
        if (strcmp(keyword, kw->keyword) == 0) {
            c->input_ptr += 3;
            kw->handler(c);
            return true;
        }
    }
    
    return false;
}

static void check_event_conflict(compiler_t *c) {
    if (c->event_building) {
        report_error(c, ERR_EXEC_CTRL_IN_EVENT);
        c->event_building = false;
    }
}

static void handle_nvc(compiler_t *c) {
    int num_voices = parse_numeric_arg(c);
    
    if (!is_valid_voice(num_voices)) {
        report_error(c, ERR_ARG_OUT_OF_RANGE);
        return;
    }
    
    check_event_conflict(c);
    emit_byte(c, OP_SET_VOICES);
    emit_byte(c, num_voices);
}

static void handle_act(compiler_t *c) {
    handle_voice_control(c, true);
}

static void handle_dct(compiler_t *c) {
    handle_voice_control(c, false);
}

static void handle_voice_control(compiler_t *c, bool activate) {
    uint8_t opcode = activate ? OP_VOICE_ACTIVATE : OP_VOICE_DEACTIVATE;
    
    do {
        skip_whitespace(c);
        int voice_num = parse_numeric_arg(c);
        int voice_idx = voice_num - 1;
        
        if (!is_valid_voice(voice_num)) {
            report_error(c, ERR_ARG_OUT_OF_RANGE);
            skip_whitespace(c);
            if (*c->input_ptr == ',') {
                c->input_ptr++;
            }
            continue;
        }
        
        check_event_conflict(c);
        emit_byte(c, opcode);
        emit_byte(c, voice_idx);
        
        if (activate) {
            activate_voice(c, voice_idx);
        } else {
            deactivate_voice(c, voice_idx);
        }
        
        skip_whitespace(c);
    } while (*c->input_ptr == ',' && ++c->input_ptr);
}

static void handle_wav(compiler_t *c) {
    skip_whitespace(c);
    int waveform = parse_numeric_arg(c);
    
    if (!is_valid_waveform(waveform)) {
        report_error(c, ERR_ARG_OUT_OF_RANGE);
        return;
    }
    
    skip_whitespace(c);
    if (*c->input_ptr != ',') {
        report_error(c, ERR_INCOMPREHENSIBLE_SPEC);
        return;
    }
    c->input_ptr++;
    
    skip_whitespace(c);
    int voice_num = parse_numeric_arg(c);
    int voice_idx = voice_num - 1;
    
    if (!is_valid_voice(voice_num)) {
        report_error(c, ERR_ARG_OUT_OF_RANGE);
        return;
    }
    
    /* Validate proper termination */
    skip_whitespace(c);
    if (*c->input_ptr != ';' && *c->input_ptr != '\0' && 
        !is_line_terminator(*c->input_ptr) && *c->input_ptr != ' ') {
        report_error(c, ERR_INCOMPREHENSIBLE_SPEC);
        while (*c->input_ptr && *c->input_ptr != ';' && !is_line_terminator(*c->input_ptr)) {
            c->input_ptr++;
        }
        return;
    }
    
    c->voices[voice_idx].use_absolute = true;
    c->voices[voice_idx].waveform = waveform - 1;  /* Store as 0-15 */
}

static void handle_tpo(compiler_t *c) {
    skip_whitespace(c);
    int tempo = parse_numeric_arg(c);
    
    if (tempo < MIN_TEMPO || tempo > MAX_TEMPO) {
        report_error(c, ERR_ARG_OUT_OF_RANGE);
        return;
    }
    
    check_event_conflict(c);
    emit_byte(c, OP_TEMPO);
    emit_byte(c, tempo);
}

static void handle_abs(compiler_t *c) {
    for (int i = 0; i < NUM_VOICES; i++) {
        c->voices[i].use_absolute = true;
    }
}

static void handle_jmp(compiler_t *c) {
    handle_jump(c, OP_JMP);
}

static void handle_jsr(compiler_t *c) {
    handle_jump(c, OP_JSR);
}

static void handle_jump(compiler_t *c, uint8_t opcode) {
    skip_whitespace(c);
    int target_id = parse_numeric_arg(c);
    
    if (target_id < 1 || target_id > 255) {
        report_error(c, ERR_ARG_OUT_OF_RANGE);
        return;
    }
    
    uint16_t target_addr;
    if (!find_symbol(c, (uint8_t)target_id, &target_addr)) {
        report_error(c, ERR_UNDEFINED_IDENTIFIER);
        check_event_conflict(c);
        return;
    }
    
    check_event_conflict(c);
    emit_byte(c, opcode);
    emit_word(c, target_addr - c->base_address);
}

static void handle_rts(compiler_t *c) {
    check_event_conflict(c);
    emit_byte(c, OP_RTS);
}

static void handle_sub(compiler_t *c) {
    if (c->sub_address != 0) {
        report_error(c, ERR_NESTED_SUB_ESB);
        check_event_conflict(c);
        return;
    }
    
    check_event_conflict(c);
    emit_byte(c, OP_JMP);
    c->sub_address = c->code_size;
    emit_word(c, 0x0000);  /* Placeholder */
}

static void handle_esb(compiler_t *c) {
    if (c->sub_address == 0) {
        report_error(c, ERR_ESB_WITHOUT_SUB);
        check_event_conflict(c);
        return;
    }
    
    check_event_conflict(c);
    
    /* Patch the jump address to point here */
    uint16_t current_addr = c->code_size + c->base_address;
    uint16_t relative_addr = current_addr - c->base_address;
    
    c->code[c->sub_address] = relative_addr & 0xFF;
    c->code[c->sub_address + 1] = (relative_addr >> 8) & 0xFF;
    
    c->sub_address = 0;
}

static void handle_end(compiler_t *c) {
    emit_byte(c, OP_END);
    c->end_flag = true;
    
    if (c->sub_address != 0) {
        report_error(c, ERR_HANGING_SUB);
    }
}
