/*
 * wavegen.c - Waveform Table Generator
 * 
 * Generates waveform tables from harmonic specifications in YAML format,
 * using the Fourier series evaluation algorithm.
 * 
 * Based on the original program by Hal Chamberlin for KIM-1/6502
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
#include <math.h>
#include <stdbool.h>
#include <yaml.h>
#include <getopt.h>

/* ============================================================================
 * Constants and Configuration
 * ============================================================================ */

#define WAVE_SIZE 256
#define MAX_HARMONICS 16
#define MIN_HARMONICS 1
#define PI 3.14159265358979323846

#define DEFAULT_PEAK 0x3F
#define DEFAULT_SEGMENT "WAVE"
#define INITIAL_LIST_CAPACITY 10

#define BYTES_PER_ROW 16
#define ROWS_PER_WAVETABLE (WAVE_SIZE / BYTES_PER_ROW)

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

typedef struct {
    char name[256];
    char desc[512];
    char segment[64];
    uint8_t peak;
    bool norm;
    uint16_t harmonics[MAX_HARMONICS + 1];  /* DC + up to 16 harmonics */
    int num_harmonics;                      /* Number of harmonics (excluding DC) */
} waveform_spec_t;

typedef struct {
    waveform_spec_t *specs;
    int count;
    int capacity;
} waveform_list_t;

/* ============================================================================
 * Harmonic Data Extraction
 * ============================================================================ */

static inline uint8_t extract_amplitude(uint16_t harmonic_data) {
    return (harmonic_data >> 8) & 0xFF;
}

static inline uint8_t extract_phase(uint16_t harmonic_data) {
    return harmonic_data & 0xFF;
}

/* ============================================================================
 * Unit Conversions
 * ============================================================================ */

static inline double byte_to_normalized_amplitude(uint8_t amplitude) {
    return amplitude / 255.0;
}

static inline double byte_to_radians(uint8_t angle_byte) {
    return (angle_byte / 256.0) * 2.0 * PI;
}

static inline uint8_t double_to_byte_saturated(double value) {
    if (value < 0.0) return 0;
    if (value > 255.0) return 255;
    return (uint8_t)(value + 0.5);  /* Round to nearest */
}

/* ============================================================================
 * Fourier Series Evaluation
 * ============================================================================ */

/*
 * Evaluates a single harmonic contribution at a given point.
 * 
 * This replicates the original 6502 assembly algorithm:
 * - angle = phase + index_accumulator (8-bit arithmetic)
 * - contribution = amplitude * cos(angle)
 */
static double evaluate_harmonic(uint16_t harmonic_data, uint8_t angle_offset) {
    const uint8_t amplitude = extract_amplitude(harmonic_data);
    const uint8_t phase = extract_phase(harmonic_data);
    
    const double normalized_amplitude = byte_to_normalized_amplitude(amplitude);
    const uint8_t angle_byte = (uint8_t)(phase + angle_offset);
    const double angle_radians = byte_to_radians(angle_byte);
    
    return normalized_amplitude * cos(angle_radians);
}

/*
 * Evaluates a waveform point using Fourier series.
 * 
 * point_index: point number (0-255)
 * spec: waveform specification
 * Returns: accumulated harmonic value
 * 
 * Algorithm matches original assembly:
 * - index_accumulator starts at 0
 * - For each harmonic: calculate and accumulate contribution
 * - Then: index_accumulator += point_index (for next iteration)
 */
static double evaluate_fourier_series(int point_index, const waveform_spec_t *spec) {
    double accumulator = 0.0;
    uint8_t index_accumulator = 0;
    
    for (int i = 0; i <= spec->num_harmonics; i++) {
        accumulator += evaluate_harmonic(spec->harmonics[i], index_accumulator);
        index_accumulator = (uint8_t)(index_accumulator + point_index);
    }
    
    return accumulator;
}

/* ============================================================================
 * Waveform Generation
 * ============================================================================ */

typedef struct {
    double min;
    double max;
} value_range_t;

static value_range_t find_waveform_range(const double *waveform, int size) {
    value_range_t range = { .min = waveform[0], .max = waveform[0] };
    
    for (int i = 1; i < size; i++) {
        if (waveform[i] < range.min) range.min = waveform[i];
        if (waveform[i] > range.max) range.max = waveform[i];
    }
    
    return range;
}

static void compute_raw_waveform(const waveform_spec_t *spec, double *waveform) {
    for (int i = 0; i < WAVE_SIZE; i++) {
        waveform[i] = evaluate_fourier_series(i, spec);
    }
}

static void normalize_and_quantize(const waveform_spec_t *spec, 
                                   const double *waveform, 
                                   uint8_t *output) {
    const value_range_t range = find_waveform_range(waveform, WAVE_SIZE);
    
    double scale = 1.0;
    double offset = 0.0;
    
    if (spec->norm) {
        const double span = range.max - range.min;
        if (span > 0.0) {
            scale = spec->peak / span;
            offset = -range.min;
        }
    }
    
    for (int i = 0; i < WAVE_SIZE; i++) {
        const double normalized = spec->norm 
            ? (waveform[i] + offset) * scale 
            : waveform[i];
        
        output[i] = double_to_byte_saturated(normalized);
    }
}

static void generate_waveform(const waveform_spec_t *spec, uint8_t *wavetable) {
    double waveform[WAVE_SIZE];
    
    compute_raw_waveform(spec, waveform);
    normalize_and_quantize(spec, waveform, wavetable);
}

/* ============================================================================
 * Waveform List Management
 * ============================================================================ */

static void init_waveform_list(waveform_list_t *list) {
    list->capacity = INITIAL_LIST_CAPACITY;
    list->count = 0;
    list->specs = malloc(list->capacity * sizeof(waveform_spec_t));
    
    if (!list->specs) {
        fprintf(stderr, "Fatal: Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
}

static void add_waveform_spec(waveform_list_t *list, const waveform_spec_t *spec) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        waveform_spec_t *new_specs = realloc(list->specs, 
                                          list->capacity * sizeof(waveform_spec_t));
        if (!new_specs) {
            fprintf(stderr, "Fatal: Memory reallocation failed\n");
            exit(EXIT_FAILURE);
        }
        list->specs = new_specs;
    }
    
    list->specs[list->count++] = *spec;
}

static void free_waveform_list(waveform_list_t *list) {
    free(list->specs);
    list->specs = NULL;
    list->count = 0;
    list->capacity = 0;
}

/* ============================================================================
 * YAML Parsing
 * ============================================================================ */

typedef struct {
    waveform_spec_t current_spec;
    char current_key[256];
    bool in_document;
    bool in_list;
    int list_index;
} parser_state_t;

static void init_parser_state(parser_state_t *state) {
    memset(state, 0, sizeof(parser_state_t));
}

static void start_new_document(parser_state_t *state) {
    memset(&state->current_spec, 0, sizeof(waveform_spec_t));
    state->current_spec.peak = DEFAULT_PEAK;
    state->current_spec.norm = true;
    strncpy(state->current_spec.segment, DEFAULT_SEGMENT, 
            sizeof(state->current_spec.segment) - 1);
    state->current_key[0] = '\0';
    state->in_document = true;
}

static void handle_list_value(parser_state_t *state, const char *value) {
    if (state->list_index <= MAX_HARMONICS) {
        state->current_spec.harmonics[state->list_index++] = 
            (uint16_t)strtol(value, NULL, 0);
    }
}

static void handle_scalar_value(parser_state_t *state, const char *value) {
    if (strcmp(state->current_key, "name") == 0) {
        strncpy(state->current_spec.name, value, 
                sizeof(state->current_spec.name) - 1);
    } else if (strcmp(state->current_key, "desc") == 0) {
        strncpy(state->current_spec.desc, value, 
                sizeof(state->current_spec.desc) - 1);
    } else if (strcmp(state->current_key, "segment") == 0) {
        strncpy(state->current_spec.segment, value, 
                sizeof(state->current_spec.segment) - 1);
    } else if (strcmp(state->current_key, "peak") == 0) {
        state->current_spec.peak = (uint8_t)strtol(value, NULL, 0);
    } else if (strcmp(state->current_key, "norm") == 0) {
        state->current_spec.norm = (strcasecmp(value, "true") == 0 || 
                                   strcmp(value, "1") == 0);
    }
}

static void handle_scalar_event(parser_state_t *state, const char *value) {
    if (state->in_list) {
        handle_list_value(state, value);
    } else if (state->current_key[0] == '\0') {
        /* This is a key */
        strncpy(state->current_key, value, sizeof(state->current_key) - 1);
    } else {
        /* This is a value */
        handle_scalar_value(state, value);
        state->current_key[0] = '\0';
    }
}

static void handle_sequence_end(parser_state_t *state) {
    state->in_list = false;
    state->current_spec.num_harmonics = state->list_index - 1;  /* Subtract DC */
    state->current_key[0] = '\0';
}

static bool process_yaml_events(yaml_parser_t *parser, waveform_list_t *list) {
    parser_state_t state;
    init_parser_state(&state);
    
    yaml_event_t event;
    bool done = false;
    
    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            fprintf(stderr, "Error: YAML parsing failed\n");
            return false;
        }
        
        switch (event.type) {
            case YAML_STREAM_END_EVENT:
                done = true;
                break;
                
            case YAML_DOCUMENT_START_EVENT:
                start_new_document(&state);
                break;
                
            case YAML_DOCUMENT_END_EVENT:
                if (state.in_document && state.current_spec.name[0]) {
                    add_waveform_spec(list, &state.current_spec);
                }
                state.in_document = false;
                break;
                
            case YAML_MAPPING_START_EVENT:
                state.current_key[0] = '\0';
                break;
                
            case YAML_SEQUENCE_START_EVENT:
                state.in_list = true;
                state.list_index = 0;
                break;
                
            case YAML_SEQUENCE_END_EVENT:
                handle_sequence_end(&state);
                break;
                
            case YAML_SCALAR_EVENT:
                handle_scalar_event(&state, (char *)event.data.scalar.value);
                break;
                
            default:
                break;
        }
        
        yaml_event_delete(&event);
    }
    
    return true;
}

static bool parse_yaml_file(const char *filename, waveform_list_t *list) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return false;
    }
    
    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        fprintf(stderr, "Error: YAML parser initialization failed\n");
        fclose(file);
        return false;
    }
    
    yaml_parser_set_input_file(&parser, file);
    const bool success = process_yaml_events(&parser, list);
    
    yaml_parser_delete(&parser);
    fclose(file);
    
    return success;
}

/* ============================================================================
 * Output Generation
 * ============================================================================ */

static void write_wavetable_header(FILE *out, const waveform_spec_t *spec) {
    fprintf(out, "; %s\n;\n", spec->desc);
    fprintf(out, ".segment \"%s\"\n", spec->segment);
    fprintf(out, ".export %s\n", spec->name);
    fprintf(out, "%s:\n", spec->name);
}

static void write_wavetable_data(FILE *out, const uint8_t *wavetable) {
    for (int row = 0; row < ROWS_PER_WAVETABLE; row++) {
        fprintf(out, "    .byte ");
        
        for (int col = 0; col < BYTES_PER_ROW; col++) {
            const int index = row * BYTES_PER_ROW + col;
            fprintf(out, "$%02X", wavetable[index]);
            
            if (col < BYTES_PER_ROW - 1) {
                fprintf(out, ",");
            }
        }
        
        fprintf(out, "\n");
    }
}

static void write_output(FILE *out, const waveform_spec_t *spec, 
                        const uint8_t *wavetable) {
    write_wavetable_header(out, spec);
    write_wavetable_data(out, wavetable);
}

/* ============================================================================
 * Waveform Processing
 * ============================================================================ */

static bool is_valid_harmonic_count(int num_harmonics) {
    return num_harmonics >= MIN_HARMONICS && num_harmonics <= MAX_HARMONICS;
}

static bool process_waveform_spec(FILE *out, const waveform_spec_t *spec) {
    if (!is_valid_harmonic_count(spec->num_harmonics)) {
        fprintf(stderr, "Warning: '%s' has %d harmonics (valid: %d-%d), skipping\n",
                spec->name, spec->num_harmonics, MIN_HARMONICS, MAX_HARMONICS);
        return false;
    }
    
    uint8_t wavetable[WAVE_SIZE];
    generate_waveform(spec, wavetable);
    write_output(out, spec, wavetable);
    
    printf("Generated: %s (%d harmonics)\n", spec->name, spec->num_harmonics);
    return true;
}

static void generate_all_waveforms(FILE *out, const waveform_list_t *list, 
                                   const char *input_filename) {
    fprintf(out, "; Waveform tables generated by wavegen\n");
    fprintf(out, "; Generated from: %s\n\n", input_filename);
    
    for (int i = 0; i < list->count; i++) {
        if (process_waveform_spec(out, &list->specs[i])) {
            if (i < list->count - 1) {
                fprintf(out, "\n");
            }
        }
    }
}

/* ============================================================================
 * Command Line Interface
 * ============================================================================ */

static void print_usage(const char *progname) {
    printf("Usage: %s [-o <output.s>] <input.yaml>\n", progname);
    printf("\nOptions:\n");
    printf("  -o <file>      Output file in CA65 assembly format\n");
    printf("                 (if not specified, uses stdout)\n");
    printf("  -h             Show this help\n");
    printf("\nThe YAML file must contain one or more documents with:\n");
    printf("  name:     Table name\n");
    printf("  desc:     Waveform description\n");
    printf("  segment:  Segment name\n");
    printf("  peak:     Peak value (0x00-0xFF)\n");
    printf("  norm:     true/false for normalization\n");
    printf("  list:     List of 2-17 hexadecimal values (DC + 1-16 harmonics)\n");
    printf("            MSB=amplitude, LSB=phase\n");
}

typedef struct {
    char *input_filename;
    char *output_filename;
} command_line_args_t;

static bool parse_command_line(int argc, char *argv[], command_line_args_t *args) {
    args->input_filename = NULL;
    args->output_filename = NULL;
    
    int opt;
    while ((opt = getopt(argc, argv, "o:h")) != -1) {
        switch (opt) {
            case 'o':
                args->output_filename = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                exit(EXIT_SUCCESS);
            default:
                print_usage(argv[0]);
                return false;
        }
    }
    
    if (optind >= argc) {
        fprintf(stderr, "Error: Missing input YAML file\n\n");
        print_usage(argv[0]);
        return false;
    }
    
    args->input_filename = argv[optind];
    return true;
}

static FILE *open_output_file(const char *filename) {
    if (!filename) {
        return stdout;
    }
    
    FILE *out = fopen(filename, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create output file '%s'\n", filename);
    }
    return out;
}

/* ============================================================================
 * Main Program
 * ============================================================================ */

int main(int argc, char *argv[]) {
    command_line_args_t args;
    if (!parse_command_line(argc, argv, &args)) {
        return EXIT_FAILURE;
    }
    
    waveform_list_t list;
    init_waveform_list(&list);
    
    if (!parse_yaml_file(args.input_filename, &list)) {
        free_waveform_list(&list);
        return EXIT_FAILURE;
    }
    
    if (list.count == 0) {
        fprintf(stderr, "Error: No valid specifications found in YAML file\n");
        free_waveform_list(&list);
        return EXIT_FAILURE;
    }
    
    FILE *out = open_output_file(args.output_filename);
    if (!out) {
        free_waveform_list(&list);
        return EXIT_FAILURE;
    }
    
    generate_all_waveforms(out, &list, args.input_filename);
    
    if (args.output_filename) {
        fclose(out);
    }
    
    free_waveform_list(&list);
    return EXIT_SUCCESS;
}