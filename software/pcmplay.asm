; K-1002 PCM Playback Example
; Plays PCM data at 8000 Hz with 1MHz system clock
;
; (C) 2025 Eduardo Casino
;

DAC         = $1700                 ; DAC output port
DACDIR      = $1701                 ; DAC direction register
KIMMON      = $1C22                 ; Entry point to KIM keyboard monitor

            .include "wav/pcmdata.inc"

            .zeropage

; Zero page temporary variables
;
PCMPTR:     .res    2               ; Current sample pointer
COUNTER:    .res    2               ; Sample counter

            .data

PCMTBL:     .word   PCM_TABLE       ; Pointer to PCM sample table
TBLSIZ:     .word   TABLE_SIZE      ; Size of table in bytes

            .code

            ; Initialize DAC port as output
            ;
            lda     #$FF
            sta     DACDIR

            cld

            ; Load table pointer into zero page
            ;
            lda     PCMTBL
            sta     PCMPTR
            lda     PCMTBL+1
            sta     PCMPTR+1
        
            ; Load table size into counter
            ;
            lda     TBLSIZ
            sta     COUNTER
            lda     TBLSIZ+1
            sta     COUNTER+1
        
            ; Check if table is empty
            ;
            ora     COUNTER         ; OR high and low bytes
            beq     pcm_done        ; Exit if zero

;---------------------------------------------------------------------------------
; Main playback loop - 125 cycles per iteration, or 8000 Hz with 1MHz system clock
;
; NOTE: This code ensures that each loop iteration takes exactly 125 cycles
;---------------------------------------------------------------------------------
pcm_loop:
            ; Load and output sample (11 cycles)
            ;
            ldy     #0              ; 2 cycles
            lda     (PCMPTR),Y      ; 5 cycles - Read sample from table
            sta     DAC             ; 4 cycles - Output to DAC
        
            ; Increment pointer with cycle balancing:
            ;
            ; Path A (no carry):    5 + 3 + 2 + 2 + 3 = 15 cycles
            ; Path B (carry):       5 + 2 + 5 + 3 = 15 cycles
            ;
            inc     PCMPTR          ; 5 cycles
            bne     inc_no_carry    ; 3 cycles (branch taken)
                                    ; 2 cycles (not taken)
            
            inc     PCMPTR+1        ; 5 cycles
            jmp     dec_counter     ; 3 cycles
inc_no_carry:
            nop                     ; 2 cycles
            nop                     ; 2 cycles
            bit     PCMPTR          ; 3 cycles (just for matching 15 cycles, does nothing meaningful)

            ; Now, decrement counter with cycle balancing as well:
            ;
            ; Path A (no borrow):   3 + 3 + 2 + 2 + 3 + 5 = 18 cycles
            ; Path B (borrow):      3 + 2 + 5 + 3 + 5 = 18 cycles
dec_counter:
            lda     COUNTER         ; 3 cycles
            bne     dec_no_borrow   ; 3 cycles (branch taken)
                                    ; 2 cycles (not taken)
            dec     COUNTER+1       ; 5 cycles
            jmp     dec_cnt_low     ; 3 cycles
dec_no_borrow:
            nop                     ; 2 cycles
            nop                     ; 2 cycles
            bit     COUNTER         ; 3 cycles (just for matching 15 cycles, does nothing meaningful)
dec_cnt_low:
            dec     COUNTER         ; 5 cycles
        
            ; Check if done (8 cycles)
            ;
            lda     COUNTER         ; 3 cycles
            ora     COUNTER+1       ; 3 cycles
            beq     pcm_done        ; 2 cycles (not taken)
                                    ; 3 cycles (taken when done)
        
            ; Timing delay loop to reach 125 cycles total
            ; Current: 11 (load) + 15 (inc) + 18 (dec) + 8 (check) = 52 cycles
            ; Need: 125 - 52 = 73 cycles
            ; Loop: NOP=2 + LDX=2 + (BIT=3 + DEX=2 + BNE=3)*N + BNE=2 + JMP=3
            ; For N=8: 2 + 2 + 8*8 + 2 + 3 = 73 cycles
            ;
            nop                     ; 2 cycles
            ldx     #8              ; 2 cycles
pcm_delay:
            bit     COUNTER         ; 3 cycles
            dex                     ; 2 cycles
            bne     pcm_delay       ; 3 cycles (branch taken)  8 * (3 + 2 + 3) = 64 cycles
                                    ; 2 cycles (branch not taken)        
            jmp     pcm_loop        ; 3 cycles - Continue loop

pcm_done:
            jmp     KIMMON

            .end
