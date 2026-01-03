/*
 * NOTRAN interpreter - Modern C implementation of the NOTRAN music
 *                      bytecode interpreter
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
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <alsa/asoundlib.h>

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */

#define SAMPLE_RATE_DEFAULT     8772
#define CHANNELS                1
#define BITS_PER_SAMPLE         8
#define BUFFER_FRAMES           1024

#define MAX_VOICES              4
#define WAVETABLE_SIZE          256
#define NUM_NOTES               62

#define PITCH_MASK              0xF0
#define DURATION_MASK           0x0F
#define PITCH_SHIFT             4

/* Control commands (duration field = 0) */
#define CMD_END                 0x00
#define CMD_TEMPO               0x10
#define CMD_CALL                0x20
#define CMD_RETURN              0x30
#define CMD_JUMP                0x40
#define CMD_SETVOICES           0x50
#define CMD_LONGNOTE_ABS        0x60
#define CMD_LONGNOTE_REL        0x70
#define CMD_DEACTIVATE          0x80
#define CMD_ACTIVATE            0x90

#define PITCH_REST              (-8)
#define VOICE_INACTIVE          0xFF
#define STACK_SIZE              256

#define SAMPLE_MIN              0
#define SAMPLE_MAX              255

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================ */

typedef struct {
    uint8_t phase_frac;
    uint8_t phase_int;
    uint8_t wavetable_page;
    uint8_t note_offset;
    uint16_t freq_increment;
    uint8_t duration;
    uint8_t padding;
} voice_t;

typedef struct {
    char riff_id[4];
    uint32_t riff_size;
    char wave_id[4];
    char fmt_id[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data_id[4];
    uint32_t data_size;
} wav_header_t;

typedef struct {
    FILE *fp;
    wav_header_t header;
    size_t samples_written;
    uint8_t *buffer;
    size_t buffer_size;
    size_t buffer_pos;
} wav_context_t;

typedef struct {
    voice_t voices[MAX_VOICES];
    uint8_t *object_code;
    size_t code_size;
    size_t code_ptr;
    uint8_t **wavetables;
    int num_wavetables;
    uint8_t tempo;
    uint8_t duration;
    uint8_t duration_counter;
    uint16_t call_stack[STACK_SIZE];
    int stack_ptr;
    int num_active_voices;
    bool running;
    uint32_t max_jumps;
} interpreter_state_t;

typedef struct {
    const char *bytecode_file;
    const char *wavetable_file;
    const char *output_file;
    int sample_rate;
    uint32_t max_jumps;
} config_t;

/* ============================================================================
 * GLOBAL DATA
 * ============================================================================ */

static const uint8_t DURATION_TABLE[16] = {
    0, 192, 144, 96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6
};

static const uint16_t FREQUENCY_TABLE[NUM_NOTES] = {
    0x0000, 0x00F4, 0x0103, 0x0112, 0x0123, 0x0134, 0x0146, 0x015A,
    0x016E, 0x0184, 0x019B, 0x01B3, 0x01CD, 0x01E9, 0x0206, 0x0225,
    0x0245, 0x0268, 0x028C, 0x02B3, 0x02DC, 0x0308, 0x0336, 0x0367,
    0x039A, 0x03D1, 0x040B, 0x0449, 0x048A, 0x04CF, 0x0519, 0x0566,
    0x05B8, 0x060F, 0x066C, 0x06CD, 0x0735, 0x07A3, 0x0817, 0x0892,
    0x0915, 0x099F, 0x0A31, 0x0ACC, 0x0B71, 0x0C1F, 0x0CD7, 0x0D9B,
    0x0E6A, 0x0F45, 0x102E, 0x1124, 0x1229, 0x133E, 0x1462, 0x1599,
    0x16E2, 0x183E, 0x19AF, 0x1B36, 0x1CD4, 0x1E8B
};

static interpreter_state_t *g_state = NULL;
static snd_pcm_t *g_pcm_handle = NULL;

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================ */

static void signal_handler(int signum);
static void cleanup(interpreter_state_t *state, snd_pcm_t *pcm_handle);
static wav_context_t *wav_open(const char *filename, int sample_rate);
static int wav_write(wav_context_t *ctx, const uint8_t *buffer, size_t count);
static int wav_close(wav_context_t *ctx);
static void print_usage(const char *program_name);
static uint8_t **load_wavetables(const char *filename, int *num_tables);
static uint8_t *load_notran_bytecode(const char *filename, size_t *size);
static int init_interpreter(interpreter_state_t *state, uint8_t *object_code,
                            size_t code_size, uint8_t **wavetables,
                            int num_wavetables, uint32_t max_jumps);
static int init_audio(snd_pcm_t **pcm_handle, int sample_rate);
static int interpret_loop(interpreter_state_t *state, snd_pcm_t *pcm_handle,
                         wav_context_t *wav_ctx);
static void free_wavetables(uint8_t **tables);

/* ============================================================================
 * Command Line Interface
 * ============================================================================ */

static void print_usage(const char *program_name) {
    printf("NOTRAN Interpreter - Music synthesis from NOTRAN bytecode\n\n");
    printf("Usage: %s [OPTIONS] <bytecode.bin> <wavetables.bin>\n\n", 
           program_name);
    printf("Options:\n");
    printf("  -o, --output FILE   Output WAV file\n");
    printf("  -r, --rate RATE     Sample rate in Hz (default: %d)\n", 
           SAMPLE_RATE_DEFAULT);
    printf("  -j, --jumps N       Maximum allowed jumps (default: unlimited)\n");
    printf("  -h, --help          Show this help\n\n");
}

static int parse_arguments(int argc, char *argv[], config_t *config) {
    *config = (config_t){
        .sample_rate = SAMPLE_RATE_DEFAULT,
        .max_jumps = UINT32_MAX
    };
    
    static struct option long_options[] = {
        {"output", required_argument, 0, 'o'},
        {"rate",   required_argument, 0, 'r'},
        {"jumps",  required_argument, 0, 'j'},
        {"help",   no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "o:r:j:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'o': config->output_file = optarg; break;
            case 'r':
                config->sample_rate = atoi(optarg);
                if (config->sample_rate < 1000 || config->sample_rate > 96000) {
                    fprintf(stderr, "Error: Invalid sample rate\n");
                    return -1;
                }
                break;
            case 'j':
                config->max_jumps = strtoul(optarg, NULL, 10);
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            default:
                return -1;
        }
    }
    
    if (optind + 2 != argc) {
        fprintf(stderr, "Error: Expected 2 arguments\n");
        print_usage(argv[0]);
        return -1;
    }
    
    config->bytecode_file = argv[optind];
    config->wavetable_file = argv[optind + 1];
    
    return 0;
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int main(int argc, char *argv[]) {
    config_t config;
    if (parse_arguments(argc, argv, &config) != 0) {
        return 1;
    }
    
    int num_wavetables;
    uint8_t **wavetables = load_wavetables(config.wavetable_file, &num_wavetables);
    if (!wavetables) {
        return 1;
    }
    
    size_t bytecode_size;
    uint8_t *bytecode = load_notran_bytecode(config.bytecode_file, &bytecode_size);
    if (!bytecode) {
        free_wavetables(wavetables);
        return 1;
    }
    
    interpreter_state_t *state = calloc(1, sizeof(interpreter_state_t));
    if (!state || init_interpreter(state, bytecode, bytecode_size, 
                                   wavetables, num_wavetables, 
                                   config.max_jumps) != 0) {
        cleanup(state, NULL);
        return 1;
    }
    
    snd_pcm_t *pcm_handle = NULL;
    wav_context_t *wav_ctx = NULL;
    
    if (config.output_file) {
        wav_ctx = wav_open(config.output_file, config.sample_rate);
        if (!wav_ctx) {
            cleanup(state, NULL);
            return 1;
        }
    } else {
        if (init_audio(&pcm_handle, config.sample_rate) != 0) {
            fprintf(stderr, "\nTip: Try WAV output: -o output.wav\n");
            cleanup(state, NULL);
            return 1;
        }
    }
    
    g_state = state;
    g_pcm_handle = pcm_handle;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Starting NOTRAN playback...\n");
    const int result = interpret_loop(state, pcm_handle, wav_ctx);
    
    if (wav_ctx) {
        wav_close(wav_ctx);
    }
    
    cleanup(state, pcm_handle);
    return (result == 0) ? 0 : 1;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

static inline uint16_t get_frequency_increment(uint8_t note_offset) {
    const int array_index = note_offset / 2;
    
    if (array_index >= NUM_NOTES || array_index < 0) {
        return 0;
    }
    
    return FREQUENCY_TABLE[array_index];
}

static inline int8_t sign_extend_4bit(uint8_t nibble) {
    int8_t value = (int8_t)nibble;
    return (value >= 8) ? (value | 0xF0) : value;
}

static inline uint8_t clamp_sample(uint16_t value) {
    return (value > SAMPLE_MAX) ? SAMPLE_MAX : (uint8_t)value;
}

static inline bool is_control_command(uint8_t command) {
    return (command & DURATION_MASK) == 0;
}

static inline bool is_long_note_command(uint8_t command) {
    const uint8_t cmd_type = command & PITCH_MASK;
    return (cmd_type == CMD_LONGNOTE_ABS || cmd_type == CMD_LONGNOTE_REL);
}

static inline bool is_voice_active(const voice_t *voice) {
    return voice->duration != VOICE_INACTIVE;
}

static inline bool is_voice_expired(const voice_t *voice) {
    return voice->duration == 0;
}

/* ============================================================================
 * Voice Management
 * ============================================================================ */

static void init_voice(voice_t *voice, uint8_t wavetable_base) {
    memset(voice, 0, sizeof(*voice));
    voice->wavetable_page = wavetable_base;
    voice->duration = VOICE_INACTIVE;
}

static void set_voice_silent(voice_t *voice) {
    voice->freq_increment = 0;
}

static void activate_voice(voice_t *voice) {
    voice->duration = 0;
    set_voice_silent(voice);
}

static void deactivate_voice(voice_t *voice) {
    voice->duration = VOICE_INACTIVE;
    set_voice_silent(voice);
}

static void reset_phase_accumulator(voice_t *voice) {
    voice->phase_frac = 0;
    voice->phase_int = 0;
}

static void update_voice_frequency(voice_t *voice, uint8_t note_offset) {
    voice->note_offset = note_offset;
    voice->freq_increment = get_frequency_increment(note_offset);
}

static void assign_short_note(voice_t *voice, uint8_t pitch_field, 
                              uint8_t duration_code) {
    const uint8_t prev_note_offset = voice->note_offset;
    voice->duration = DURATION_TABLE[duration_code];
    
    const int8_t pitch_nibble = sign_extend_4bit(pitch_field >> PITCH_SHIFT);
    
    if (pitch_nibble == PITCH_REST) {
        set_voice_silent(voice);
        return;
    }
    
    const int8_t byte_offset = pitch_nibble * 2;
    voice->note_offset += byte_offset;
    update_voice_frequency(voice, voice->note_offset);
    
    if (byte_offset == 0 && prev_note_offset == voice->note_offset) {
        reset_phase_accumulator(voice);
    }
}

static void assign_long_note_absolute(voice_t *voice, uint8_t pitch_byte,
                                      uint8_t waveform, uint8_t duration_code) {
    voice->note_offset = pitch_byte;
    voice->wavetable_page = waveform;
    voice->duration = DURATION_TABLE[duration_code];
    update_voice_frequency(voice, pitch_byte);
}

static void assign_long_note_relative(voice_t *voice, int8_t pitch_displacement,
                                      uint8_t waveform, uint8_t duration_code) {
    voice->note_offset += pitch_displacement;
    voice->wavetable_page = waveform;
    voice->duration = DURATION_TABLE[duration_code];
    update_voice_frequency(voice, voice->note_offset);
}

/* ============================================================================
 * Interpreter State
 * ============================================================================ */

static int init_interpreter(interpreter_state_t *state,
                            uint8_t *object_code,
                            size_t code_size,
                            uint8_t **wavetables,
                            int num_wavetables,
                            uint32_t max_jumps) {
    if (!state || !object_code || !wavetables) {
        fprintf(stderr, "Error: NULL parameter in init_interpreter\n");
        return -1;
    }
    
    if (code_size == 0 || num_wavetables == 0) {
        fprintf(stderr, "Error: Invalid code_size or num_wavetables\n");
        return -1;
    }
    
    memset(state, 0, sizeof(*state));
    
    state->object_code = object_code;
    state->code_size = code_size;
    state->wavetables = wavetables;
    state->num_wavetables = num_wavetables;
    state->num_active_voices = MAX_VOICES;
    state->running = true;
    state->max_jumps = max_jumps;
    
    for (int i = 0; i < MAX_VOICES; i++) {
        init_voice(&state->voices[i], 0);
    }
    
    return 0;
}

static void set_num_voices(interpreter_state_t *state, int num_voices) {
    if (num_voices < 1) {
        num_voices = 1;
    } else if (num_voices > MAX_VOICES) {
        num_voices = MAX_VOICES;
    }
    state->num_active_voices = num_voices;
}

/* ============================================================================
 * Bytecode Reading
 * ============================================================================ */

static inline uint8_t read_code_byte(interpreter_state_t *state) {
    if (state->code_ptr >= state->code_size) {
        return 0;
    }
    return state->object_code[state->code_ptr++];
}

static inline uint16_t read_code_address(interpreter_state_t *state) {
    const uint8_t low = read_code_byte(state);
    const uint8_t high = read_code_byte(state);
    return (uint16_t)low | ((uint16_t)high << 8);
}

/* ============================================================================
 * Command Processing
 * ============================================================================ */

static int handle_tempo_command(interpreter_state_t *state) {
    const uint8_t new_tempo = read_code_byte(state);
    if (new_tempo == 0) {
        fprintf(stderr, "Error: Tempo cannot be zero at position %zu\n",
                state->code_ptr - 2);
        return -1;
    }
    state->tempo = new_tempo;
    return 0;
}

static int handle_call_command(interpreter_state_t *state) {
    if (state->stack_ptr >= STACK_SIZE) {
        fprintf(stderr, "Error: Call stack overflow at position %zu\n",
                state->code_ptr - 1);
        return -1;
    }
    
    state->call_stack[state->stack_ptr++] = state->code_ptr + 2;
    
    const uint16_t addr = read_code_address(state);
    if (addr >= state->code_size) {
        fprintf(stderr, "Error: Call to invalid address 0x%04X at position %zu\n",
                addr, state->code_ptr - 3);
        return -1;
    }
    
    state->code_ptr = addr;
    return 0;
}

static int handle_return_command(interpreter_state_t *state) {
    if (state->stack_ptr == 0) {
        fprintf(stderr, "Error: Return with empty call stack at position %zu\n",
                state->code_ptr - 1);
        return -1;
    }
    
    state->code_ptr = state->call_stack[--state->stack_ptr];
    return 0;
}

static int handle_jump_command(interpreter_state_t *state) {
    if (state->max_jumps == 0) {
        fprintf(stderr, "Info: Maximum jump limit reached at position %zu\n",
                state->code_ptr - 1);
        return 1;
    }
    
    --state->max_jumps;
    
    const uint16_t addr = read_code_address(state);
    if (addr >= state->code_size) {
        fprintf(stderr, "Error: Jump to invalid address 0x%04X at position %zu\n",
                addr, state->code_ptr - 3);
        return -1;
    }
    
    state->code_ptr = addr;
    return 0;
}

static int handle_setvoices_command(interpreter_state_t *state) {
    const uint8_t num_voices = read_code_byte(state);
    if (num_voices < 1 || num_voices > MAX_VOICES) {
        fprintf(stderr, "Warning: Invalid voice count %d at position %zu\n",
                num_voices, state->code_ptr - 2);
    }
    set_num_voices(state, num_voices);
    return 0;
}

static int handle_deactivate_command(interpreter_state_t *state) {
    const uint8_t voice_num = read_code_byte(state) & 0x03;
    deactivate_voice(&state->voices[voice_num]);
    return 0;
}

static int handle_activate_command(interpreter_state_t *state) {
    const uint8_t voice_num = read_code_byte(state) & 0x03;
    activate_voice(&state->voices[voice_num]);
    return 0;
}

static int process_control_command(interpreter_state_t *state, uint8_t command) {
    const uint8_t cmd_type = command & PITCH_MASK;
    
    if (is_long_note_command(command)) {
        fprintf(stderr, "Error: Long note command 0x%02X in control processing "
                "at position %zu\n", command, state->code_ptr - 1);
        return -1;
    }
    
    switch (cmd_type) {
        case CMD_END:        return 1;
        case CMD_TEMPO:      return handle_tempo_command(state);
        case CMD_CALL:       return handle_call_command(state);
        case CMD_RETURN:     return handle_return_command(state);
        case CMD_JUMP:       return handle_jump_command(state);
        case CMD_SETVOICES:  return handle_setvoices_command(state);
        case CMD_DEACTIVATE: return handle_deactivate_command(state);
        case CMD_ACTIVATE:   return handle_activate_command(state);
        default:
            fprintf(stderr, "Error: Undefined control command 0x%02X at position %zu\n",
                    command, state->code_ptr - 1);
            return -1;
    }
}

static void process_long_note(interpreter_state_t *state, voice_t *voice, 
                              uint8_t command) {
    const uint8_t cmd_type = command & PITCH_MASK;
    const uint8_t pitch_byte = read_code_byte(state);
    const uint8_t wd_byte = read_code_byte(state);
    
    uint8_t waveform = (wd_byte >> 4) & 0x0F;
    uint8_t duration_code = wd_byte & 0x0F;
    
    if (duration_code == 0) {
        fprintf(stderr, "Warning: Long note with duration code 0 at position %zu\n",
                state->code_ptr - 3);
        duration_code = 1;
    }
    
    if (waveform >= state->num_wavetables) {
        fprintf(stderr, "Warning: Invalid wavetable %d at position %zu\n",
                waveform, state->code_ptr - 3);
        waveform = state->num_wavetables - 1;
    }
    
    if (cmd_type == CMD_LONGNOTE_ABS) {
        assign_long_note_absolute(voice, pitch_byte, waveform, duration_code);
    } else {
        assign_long_note_relative(voice, (int8_t)pitch_byte, waveform, duration_code);
    }
}

static uint8_t find_shortest_duration(const interpreter_state_t *state) {
    uint8_t shortest = VOICE_INACTIVE;
    
    for (int i = 0; i < MAX_VOICES; i++) {
        const voice_t *voice = &state->voices[i];
        
        if (is_voice_active(voice) && !is_voice_expired(voice)) {
            if (voice->duration < shortest) {
                shortest = voice->duration;
            }
        }
    }
    
    return shortest;
}

/* ============================================================================
 * Synthesis Engine
 * ============================================================================ */

static inline void advance_phase(voice_t *voice) {
    uint16_t phase = ((uint16_t)voice->phase_int << 8) | voice->phase_frac;
    phase += voice->freq_increment;
    voice->phase_frac = phase & 0xFF;
    voice->phase_int = (phase >> 8) & 0xFF;
}

static inline uint8_t generate_sample(interpreter_state_t *state) {
    uint16_t sum = 0;
    
    for (int i = 0; i < state->num_active_voices; i++) {
        voice_t *voice = &state->voices[i];
        
        if (voice->freq_increment == 0 || 
            voice->wavetable_page >= state->num_wavetables) {
            continue;
        }
        
        const uint8_t *wavetable = state->wavetables[voice->wavetable_page];
        sum += wavetable[voice->phase_int];
        advance_phase(voice);
    }
    
    return clamp_sample(sum);
}

static int write_audio_buffer(snd_pcm_t *pcm_handle, wav_context_t *wav_ctx,
                              const uint8_t *buffer, size_t count) {
    if (wav_ctx) {
        return wav_write(wav_ctx, buffer, count);
    }
    
    if (!pcm_handle) {
        return 0;
    }
    
    snd_pcm_sframes_t frames = snd_pcm_writei(pcm_handle, buffer, count);
    
    if (frames < 0) {
        frames = snd_pcm_recover(pcm_handle, frames, 0);
        if (frames < 0) {
            fprintf(stderr, "Error: snd_pcm_writei failed: %s\n",
                    snd_strerror(frames));
            return -1;
        }
    }
    
    return 0;
}

static int play_notes(interpreter_state_t *state, snd_pcm_t *pcm_handle,
                     wav_context_t *wav_ctx, uint8_t *buffer, size_t buffer_size) {
    const int total_samples = state->tempo * state->duration;
    int samples_generated = 0;
    size_t buffer_pos = 0;
    
    while (samples_generated < total_samples && state->running) {
        buffer[buffer_pos++] = generate_sample(state);
        samples_generated++;
        
        if (buffer_pos >= buffer_size) {
            if (write_audio_buffer(pcm_handle, wav_ctx, buffer, buffer_pos) != 0) {
                return -1;
            }
            buffer_pos = 0;
        }
    }
    
    if (buffer_pos > 0) {
        if (write_audio_buffer(pcm_handle, wav_ctx, buffer, buffer_pos) != 0) {
            return -1;
        }
    }
    
    return 0;
}

/* ============================================================================
 * Interpreter Main Loop
 * ============================================================================ */

static int process_pure_control_commands(interpreter_state_t *state) {
    while (state->code_ptr < state->code_size) {
        const uint8_t command = state->object_code[state->code_ptr];
        
        if (!is_control_command(command) || is_long_note_command(command)) {
            break;
        }
        
        state->code_ptr++;
        const int result = process_control_command(state, command);
        if (result != 0) {
            return result;
        }
    }
    
    return 0;
}

static int process_notes_for_voices(interpreter_state_t *state) {
    int notes_assigned = 0;
    
    for (int voice_idx = 0; voice_idx < MAX_VOICES; voice_idx++) {
        voice_t *voice = &state->voices[voice_idx];
        
        if (!is_voice_active(voice)) {
            continue;
        }
        
        if (voice->duration > 0 && state->duration > 0) {
            if (voice->duration > state->duration) {
                voice->duration -= state->duration;
                continue;
            }
            voice->duration = 0;
        }
        
        if (!is_voice_expired(voice)) {
            continue;
        }
        
        if (state->code_ptr >= state->code_size) {
            break;
        }
        
        const uint8_t command = read_code_byte(state);
        const uint8_t duration_code = command & DURATION_MASK;
        
        if (duration_code == 0) {
            if (is_long_note_command(command)) {
                process_long_note(state, voice, command);
                notes_assigned++;
            } else {
                state->code_ptr--;
                return notes_assigned;
            }
        } else {
            const uint8_t pitch_field = command & PITCH_MASK;
            assign_short_note(voice, pitch_field, duration_code);
            notes_assigned++;
        }
    }
    
    return notes_assigned;
}

static int interpret_loop(interpreter_state_t *state, snd_pcm_t *pcm_handle,
                         wav_context_t *wav_ctx) {
    uint8_t *audio_buffer = malloc(BUFFER_FRAMES);
    if (!audio_buffer) {
        fprintf(stderr, "Error: Failed to allocate audio buffer\n");
        return -1;
    }
    
    if (state->tempo == 0) {
        fprintf(stderr, "Warning: Tempo not set, using default of 32\n");
        state->tempo = 32;
    }
    
    while (state->running && state->code_ptr < state->code_size) {
        const int pcc_result = process_pure_control_commands(state);
        if (pcc_result != 0) {
            free(audio_buffer);
            return (pcc_result > 0) ? 0 : -1;
        }
        
        if (state->code_ptr >= state->code_size) {
            break;
        }
        
        process_notes_for_voices(state);
        
        state->duration = find_shortest_duration(state);
        
        if (state->duration == VOICE_INACTIVE || state->duration == 0) {
            continue;
        }
        
        if (play_notes(state, pcm_handle, wav_ctx, audio_buffer, BUFFER_FRAMES) != 0) {
            free(audio_buffer);
            return -1;
        }
    }
    
    if (pcm_handle) {
        snd_pcm_drain(pcm_handle);
    }
    
    puts("Interpretation complete");
    free(audio_buffer);
    return 0;
}

/* ============================================================================
 * Audio Backend
 * ============================================================================ */

static int init_audio(snd_pcm_t **pcm_handle, int sample_rate) {
    snd_pcm_hw_params_t *hw_params;
    const char *device = "default";
    
    int err = snd_pcm_open(pcm_handle, device, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "Error: Cannot open audio device '%s': %s\n",
                device, snd_strerror(err));
        return -1;
    }
    
    if (snd_pcm_hw_params_malloc(&hw_params) < 0) {
        fprintf(stderr, "Error: Cannot allocate hardware parameter structure\n");
        snd_pcm_close(*pcm_handle);
        return -1;
    }
    
    snd_pcm_hw_params_any(*pcm_handle, hw_params);
    snd_pcm_hw_params_set_access(*pcm_handle, hw_params,
                                  SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(*pcm_handle, hw_params, SND_PCM_FORMAT_U8);
    snd_pcm_hw_params_set_channels(*pcm_handle, hw_params, CHANNELS);
    
    unsigned int actual_rate = sample_rate;
    snd_pcm_hw_params_set_rate_near(*pcm_handle, hw_params, &actual_rate, 0);
    
    snd_pcm_uframes_t buffer_size = BUFFER_FRAMES * 4;
    snd_pcm_hw_params_set_buffer_size_near(*pcm_handle, hw_params, &buffer_size);
    
    err = snd_pcm_hw_params(*pcm_handle, hw_params);
    snd_pcm_hw_params_free(hw_params);
    
    if (err < 0) {
        fprintf(stderr, "Error: Cannot set hardware parameters: %s\n",
                snd_strerror(err));
        snd_pcm_close(*pcm_handle);
        return -1;
    }
    
    if (snd_pcm_prepare(*pcm_handle) < 0) {
        fprintf(stderr, "Error: Cannot prepare audio interface\n");
        snd_pcm_close(*pcm_handle);
        return -1;
    }
    
    printf("Audio initialized: %s @ %d Hz\n", device, actual_rate);
    return 0;
}

static void close_audio(snd_pcm_t *pcm_handle) {
    if (pcm_handle) {
        snd_pcm_drain(pcm_handle);
        snd_pcm_close(pcm_handle);
    }
}

/* ============================================================================
 * File I/O
 * ============================================================================ */

static uint8_t *load_binary_file(const char *filename, size_t *size) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n",
                filename, strerror(errno));
        return NULL;
    }
    
    if (fseek(fp, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error: Cannot seek in file '%s'\n", filename);
        fclose(fp);
        return NULL;
    }
    
    const long pos = ftell(fp);
    if (pos == -1) {
        fprintf(stderr, "Error: Cannot get file size '%s'\n", filename);
        fclose(fp);
        return NULL;
    }
    
    *size = (size_t)pos;
    fseek(fp, 0, SEEK_SET);
    
    uint8_t *buffer = malloc(*size);
    if (!buffer) {
        fprintf(stderr, "Error: Cannot allocate %zu bytes\n", *size);
        fclose(fp);
        return NULL;
    }
    
    const size_t bytes_read = fread(buffer, 1, *size, fp);
    fclose(fp);
    
    if (bytes_read != *size) {
        fprintf(stderr, "Error: Read %zu bytes, expected %zu\n", bytes_read, *size);
        free(buffer);
        return NULL;
    }
    
    return buffer;
}

static uint8_t **load_wavetables(const char *filename, int *num_tables) {
    size_t file_size;
    uint8_t *file_data = load_binary_file(filename, &file_size);
    if (!file_data) {
        return NULL;
    }
    
    if (file_size % WAVETABLE_SIZE != 0) {
        fprintf(stderr, "Warning: File size not multiple of %d bytes\n", 
                WAVETABLE_SIZE);
    }
    
    const int num = file_size / WAVETABLE_SIZE;
    if (num == 0) {
        fprintf(stderr, "Error: File too small for wavetable\n");
        free(file_data);
        return NULL;
    }
    
    uint8_t **tables = malloc(num * sizeof(uint8_t *));
    if (!tables) {
        fprintf(stderr, "Error: Cannot allocate wavetable array\n");
        free(file_data);
        return NULL;
    }
    
    for (int i = 0; i < num; i++) {
        tables[i] = file_data + (i * WAVETABLE_SIZE);
    }
    
    *num_tables = num;
    printf("Loaded %d wavetable%s (%zu bytes)\n", num, 
           (num == 1) ? "" : "s", file_size);
    
    return tables;
}

static void free_wavetables(uint8_t **tables) {
    if (tables) {
        free(tables[0]);
        free(tables);
    }
}

static uint8_t *load_notran_bytecode(const char *filename, size_t *size) {
    uint8_t *bytecode = load_binary_file(filename, size);
    if (bytecode) {
        printf("Loaded NOTRAN bytecode (%zu bytes)\n", *size);
    }
    return bytecode;
}

/* ============================================================================
 * WAV File Output
 * ============================================================================ */

static wav_context_t *wav_open(const char *filename, int sample_rate) {
    wav_context_t *ctx = calloc(1, sizeof(wav_context_t));
    if (!ctx) {
        return NULL;
    }
    
    ctx->fp = fopen(filename, "wb");
    if (!ctx->fp) {
        fprintf(stderr, "Error: Cannot create WAV file '%s'\n", filename);
        free(ctx);
        return NULL;
    }
    
    ctx->buffer_size = BUFFER_FRAMES;
    ctx->buffer = malloc(ctx->buffer_size);
    if (!ctx->buffer) {
        fclose(ctx->fp);
        free(ctx);
        return NULL;
    }
    
    memcpy(ctx->header.riff_id, "RIFF", 4);
    memcpy(ctx->header.wave_id, "WAVE", 4);
    memcpy(ctx->header.fmt_id, "fmt ", 4);
    ctx->header.fmt_size = 16;
    ctx->header.audio_format = 1;
    ctx->header.num_channels = CHANNELS;
    ctx->header.sample_rate = sample_rate;
    ctx->header.bits_per_sample = BITS_PER_SAMPLE;
    ctx->header.byte_rate = sample_rate * CHANNELS * BITS_PER_SAMPLE / 8;
    ctx->header.block_align = CHANNELS * BITS_PER_SAMPLE / 8;
    memcpy(ctx->header.data_id, "data", 4);
    
    fwrite(&ctx->header, sizeof(wav_header_t), 1, ctx->fp);
    printf("WAV file opened: '%s' @ %d Hz\n", filename, sample_rate);
    
    return ctx;
}

static int wav_write(wav_context_t *ctx, const uint8_t *buffer, size_t count) {
    for (size_t i = 0; i < count; i++) {
        ctx->buffer[ctx->buffer_pos++] = buffer[i];
        
        if (ctx->buffer_pos >= ctx->buffer_size) {
            const size_t written = fwrite(ctx->buffer, 1, ctx->buffer_pos, ctx->fp);
            if (written != ctx->buffer_pos) {
                fprintf(stderr, "Error: WAV write failed\n");
                return -1;
            }
            ctx->samples_written += ctx->buffer_pos;
            ctx->buffer_pos = 0;
        }
    }
    return 0;
}

static int wav_close(wav_context_t *ctx) {
    if (!ctx) {
        return -1;
    }
    
    if (ctx->buffer_pos > 0) {
        fwrite(ctx->buffer, 1, ctx->buffer_pos, ctx->fp);
        ctx->samples_written += ctx->buffer_pos;
    }
    
    ctx->header.data_size = ctx->samples_written;
    ctx->header.riff_size = 36 + ctx->header.data_size;
    
    fseek(ctx->fp, 0, SEEK_SET);
    fwrite(&ctx->header, sizeof(wav_header_t), 1, ctx->fp);
    
    printf("WAV file closed: %zu samples (%.2f seconds)\n",
           ctx->samples_written,
           (double)ctx->samples_written / ctx->header.sample_rate);
    
    free(ctx->buffer);
    fclose(ctx->fp);
    free(ctx);
    
    return 0;
}

/* ============================================================================
 * Signal Handling and Cleanup
 * ============================================================================ */

static void signal_handler(int signum) {
    (void)signum;
    if (g_state) {
        g_state->running = false;
    }
}

static void cleanup(interpreter_state_t *state, snd_pcm_t *pcm_handle) {
    if (pcm_handle) {
        close_audio(pcm_handle);
    }
    
    if (state) {
        free(state->object_code);
        if (state->wavetables) {
            free_wavetables(state->wavetables);
        }
        free(state);
    }
}
