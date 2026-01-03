;        COPYRIGHT 1977 BY MICRO TECHNOLOGY UNLIMITED, BOX 4596,
;        MANCHESTER, NEW HAMPSHIRE 03108
;        PROGRAM WRITTEN BY HAL CHAMBERLIN

;        THIS PROGRAM PLAYS MUSIC IN 4-PART HARMONY ON THE KIM-1 OR
;        OTHER 6502 BASED SYSTEM USING AN 8-BIT UNSIGNED
;        DIGITAL-TO-ANALOG CONVERTER CONNECTED TO AN OUTPUT PORT.  TUNED
;        FOR SYSTEMS WITH A 1 MHZ CRYSTAL CLOCK.  DOES NOT USE THE ROR
;        INSTRUCTION.

;        SONG TABLE IS AT "SONG" (HEX 0200)
;        ENTRY POINT IS AT "PLAY" (HEX 0100)

;        IN THE SONG TABLE EACH MUSICAL EVENT CONSISTS OF 5 BYTES.  THE
;        FIRST BYTE IS THE DURATION OF THE EVENT IN UNITS ACCORDING TO
;        THE VALUE OF "TEMPO".  FOR A TYPICAL TEMPO SETTING OF 109
;        (DECIMAL) A DURATION OF 48 WOULD CORRESPOND TO A QUARTER NOTE
;        AT APPROXIMATELY 100 BEATS PER MINUTE.
;        IF THE FIRST BYTE IS 0 - 4, THEN A CONTROL FUNCTION IS
;        IS PERFORMED AS FOLLOWS:
;        0      END OF SONG, CONTROL IS RETURNED TO THE KIM MONITOR.
;        1      END OF SONG TABLE SEGMENT, NEXT 2 BYTES CONTAIN ADDRESS
;               OF BEGINNING OF NEXT SEGMENT.
;        2      "REFRAIN DESIGNATOR" (JUMP TO SUBROUTINE) THE CURRENT
;               LOCATION IN THE SONG TABLE IS SAVED ON THE STACK AND THE
;               NEXT 2 BYTES CONTAIN THE ADDRESS OF THE REFRAIN.
;        3      END OF REFRAIN (RETURN FROM SUBROUTINE) PLAYING RESUMES
;               AT THE LOCATION IN THE SONG TABLE SAVED BY THE LAST
;               REFRAIN DESIGNATOR.
;        NOTE:  MUSICAL SUBROUTINES MAY BE STACKED AS DEEP AS DESIRED
;               SUBJECT ONLY TO THE AMOUNT OF STACK AREA AVAILABLE.

;        THE NEXT 4 BYTES ARE NOTE ID NUMBERS FOR THE 4 SIMULTANEOUS
;        VOICES WITH VOICE 1 FIRST.  SEE THE NOTE FREQUENCY TABLE FOR
;        CORRESPONDANCE BETWEEN NUMBERS AND ACTUAL NOTES.

;        THE WAVEFORM TABLE CONTAINS A TABULATION OF THE WAVEFORM TO BE
;        PLAYED BY A VOICE.  WITH A BASIC KIM-1 (1.1K OF MEMORY) ALL
;        VOICES MUST USE THE SAME WAVEFORM.  FOR LARGER MEMORY
;        CONFIGURATIONS EACH VOICE MAY USE A DIFFERENT WAVEFORM TABLE.
;        WHEN USING A DIFFERENT WAVEFORM TABLE FOR VOICE 1 AS AN
;        EXAMPLE, THE PAGE NUMBER OF THE TABLE MUST BE ENTERED AT
;        V1PT+2; LIKEWISE AT V2PT+2 FOR VOICE 2, ETC.   THE AMPLITUDE
;        OF THE WAVEFORM TABULATED MUST BE SUCH THAT WHEN THE 4 VOICES
;        ARE ADDED UP, THERE IS NO POSSIBILITY OF OVERFLOW.  ALSO, THE
;        WAVEFORM SAMPLE VALUES ARE UNSIGNED, POSITIVE NUMBERS.  THE NO
;        OVERFLOW PROVISION IS SATISFIED IF THE LARGEST SAMPLE VALUE IN
;        A WAVEFORM TABLE IS 3F (HEX) OR LESS.  HOWEVER SOME VOICES MAY
;        BE MADE LOUDER THAN OTHERS BY ADJUSTING THE WAVEFORM
;        AMPLITUDES.  NOTE THAT MAXIMUM PERMISSABLE WAVEFORM AMPLITUDE
;        SHOULD ALWAYS BE USED TO MAXIMIZE SIGNAL-TO-NOISE RATIO.

         .zeropage           ; ORG AT PAGE 0 LOCATION 0

DAC      =      $1700        ; OUTPUT PORT ADDRESS WITH DAC
DACDIR   =      $1701        ; DATA DRIECTION REGISTER FOR DAC PORT
AUXRAM   =      $1780        ; ADDRESS OF EXTRA 128 BYTES OF RAM IN 6530

KIMMON   =      $1C22        ; ENTRY POINT TO KIM KEYBOARD MONITOR

V1PT:    .BYTE  0            ; VOICE 1 WAVE POINTER, FRACTIONAL PART
         .BYTE  0            ; INTEGER PART
         .BYTE  >WAV1TB      ; WAVEFORM TABLE PAGE ADDRESS FOR VOICE 1
V2PT:    .BYTE  0            ; SAME AS ABOVE FOR VOICE 2
         .BYTE  0
         .BYTE  >WAV2TB
V3PT:    .BYTE  0            ; SAME AS ABOVE FOR VOICE 3
         .BYTE  0
         .BYTE  >WAV3TB
V4PT:    .BYTE  0            ; SAME AS ABOVE FOR VOICE 4
         .BYTE  0
         .BYTE  >WAV4TB

V1IN:    .WORD  0            ; VOICE 1 INCREMENT (FREQUENCY PARAMETER)
V2IN:    .WORD  0            ; VOICE 2
V3IN:    .WORD  0            ; VOICE 3
V4IN:    .WORD  0            ; VOICE 4

SONGA:   .WORD  SONG         ; ADDRESS OF SONG
TEMPO:   .BYTE  182          ; TEMPO CONTROL VALUE, TYPICAL VALUE FOR
                             ; 4:4 TIME, 60 BEATS PER MINUTE, DURATION
                             ; BYTE = 48 (10) DESIGNATES A QUARTER NOTE

DUR:     .BYTE  0            ; DURATION COUNTER
NOTES:   .WORD  0            ; NOTES POINTER
INCPT:   .WORD  0            ; POINTER FOR LOADING UP V1NT - V4NT
INCA:    .WORD  V1IN         ; INITIAL VALUE OF INCPT

;        NOTE FREQUENCY TABLE FOR 8.772 KHZ SAMPLE RATE
;        RANGE FROM C2 (65.41 HZ) TO C6 (1046.5 HZ)
;                               ID  NOTE  FREQ.   INCR.
FRQTAB:  .BYTE  0,0          ;   0  SILENCE
         .BYTE  1,233        ;   2  C2    65.405  1.9089
         .BYTE  2,6          ;   4  C2#   69.295  2.0224
         .BYTE  2,37         ;   6  D2    73.415  2.1427
         .BYTE  2,69         ;   8  D2#   77.783  2.2701
         .BYTE  2,104        ;   A  E2    82.408  2.4051
         .BYTE  2,140        ;   C  F2    87.308  2.5481
         .BYTE  2,179        ;   E  F2#   92.498  2.6996
         .BYTE  2,220        ;  10  G2    97.998  2.8601
         .BYTE  3,8          ;  12  G2#   103.83  3.0302
         .BYTE  3,54         ;  14  A2    110.00  3.2104
         .BYTE  3,103        ;  16  A2#   116.54  3.4013
         .BYTE  3,154        ;  18  B2    123.47  3.6035
         .BYTE  3,209        ;  1A  C3    130.81  3.8178
         .BYTE  4,11         ;  1C  C3#   138.59  4.0448
         .BYTE  4,73         ;  1E  D3    146.83  4.2854
         .BYTE  4,138        ;  20  D3#   155.57  4.5402
         .BYTE  4,207        ;  22  E3    164.82  4.8102
         .BYTE  5,25         ;  24  F3    174.62  5.0962
         .BYTE  5,102        ;  26  F3#   185.00  5.3992
         .BYTE  5,184        ;  28  G3    196.00  5.7203
         .BYTE  6,15         ;  2A  G3#   207.65  6.0604
         .BYTE  6,108        ;  2C  A3    220.00  6.4208
         .BYTE  6,205        ;  2E  A3#   233.08  6.8026
         .BYTE  7,53         ;  30  B3    246.94  7.2071
         .BYTE  7,163        ;  32  C4    261.62  7.6356
         .BYTE  8,23         ;  34  C4#   277.18  8.0897
         .BYTE  8,146        ;  36  D4    293.66  8.5707
         .BYTE  9,21         ;  38  D4#   311.13  9.0804
         .BYTE  9,159        ;  3A  E4    329.63  9.6203
         .BYTE  10,49        ;  3C  F4    349.23  10.1924
         .BYTE  10,204       ;  3E  F4#   369.99  10.7984
         .BYTE  11,113       ;  40  G4    391.99  11.4405
         .BYTE  12,31        ;  42  G4#   415.30  12.1208
         .BYTE  12,215       ;  44  A4    440.00  12.8416
         .BYTE  13,155       ;  46  A4#   466.16  13.6052
         .BYTE  14,106       ;  48  B4    493.88  14.4142
         .BYTE  15,69        ;  4A  C5    523.24  15.2713
         .BYTE  16,46        ;  4C  C5#   554.36  16.1794
         .BYTE  17,36        ;  4E  D5    587.32  17.1414
         .BYTE  18,41        ;  50  DS#   622.26  18.1607
         .BYTE  19,62        ;  52  E5    659.26  19.2406
         .BYTE  20,98        ;  54  F5    698.46  20.3847
         .BYTE  21,153       ;  56  F5#   739.98  21.5969
         .BYTE  22,226       ;  58  G5    783.98  22.8811
         .BYTE  24,62        ;  5A  G5#   830.60  24.2417
         .BYTE  25,175       ;  5C  A5    880.00  25.6831
         .BYTE  27,54        ;  5E  A5#   932.32  27.2103
         .BYTE  28,212       ;  60  B5    987.76  28.8283
         .BYTE  30,139       ;  62  C6    1046.5  30.5426

         .segment "PAGE1"    ; START PROGRAM CODE AT LOCATION 0100

;        MAIN MUSIC PLAYING PROGRAM

MUSIC:   LDA    #$FF         ; SET PERIPHERAL A DATA DIRECTION
         STA    DACDIR       ; REGISTER TO OUTPUT
         CLD                 ; INSURE BINARY ARITHMETIC
         LDA    SONGA        ; INITIALIZE NOTES POINTER
         STA    NOTES        ; TO BEGINNING OF SONG
         LDA    SONGA+1
         STA    NOTES+1
MUSIC1:  LDY    #0           ; SET UP TO TRANSLATE 4 NOTE ID NUMBERS
         LDA    INCA         ; INTO FREQUENCY DETERMINING WAVEFORM TABLE
         STA    INCPT        ; INCREMENTS AND STORE IN V1IN - V4IN
         LDA    (NOTES),Y    ; GET DURATION FIRST
         BEQ    ENDSNG       ; BRANCH IF END OF SONG
         CMP    #2           ; TEST FOR OTHER CONTROL FUNCTIONS
         BEQ    RFNCAL       ; BRANCH IF REFRAIN CALL
         BCC    NXTSEG       ; BRANCH IF END OF TABLE SEGMENT
         CMP    #3
         BEQ    RFNRET       ; BRANCH IF RETURN FROM REFRAIN
         STA    DUR          ; OTHERWISE SAVE DURATION IN DUR
MUSIC2:  JSR    DINNOT       ; DOUBLE INCREMENT NOTES TO POINT TO THE
                             ; NOTE ID OF THE FIRST VOICE
MUSIC3:  LDA    (NOTES),Y    ; GET A NOTE ID NUMBER
         TAX                 ; INTO INDEX X
         LDA    FRQTAB+1,X   ; GET LOW BYTE OF CORRESPONDING FREQUENCY
         STA    (INCPT),Y    ; STORE INTO LOW BYTE OF VOICE INCREMENT
         INC    INCPT        ; INDEX TO HIGH BYTE
         LDA    FRQTAB,X     ; GET HIGH BYTE OF FREQUENCY
         STA    (INCPT),Y    ; STORE INTO HIGH BYTE OF VOICE INCREMENT
         JSR    DINNOT       ; DOUBLE INCREMENT NOTES TO POINT TO THE
                             ; NOTE ID OF THE NEXT VOICE
         INC    INCPT        ; INDEX TO NEXT VOICE INCREMENT
         LDA    INCPT        ; TEST IF 4 VOICE INCREMENTS DONE
         CMP    #V4IN+2
         BNE    MUSIC3       ; LOOP IF NOT
         JSR    PLAY         ; PLAY THIS GROUP OF NOTES
         JMP    MUSIC1       ; GO LOAD UP NEXT SET OF NOTES

ENDSNG:  JMP    KIMMON       ; END OF SONG, RETURN TO MONITOR

NXTSEG:  INY                 ; END OF SEGMENT, NEXT TWO BYTES POINT TO
         LDA    (NOTES),Y    ; BEGINNING OF THE NEXT SEGMENT
         PHA
         INY                 ; GET BOTH SEGMENT ADDRESS BYTES
         LDA    (NOTES),Y
         STA    NOTES+1      ; THEN STORE IN NOTES POINTER
         PLA
         STA    NOTES
         JMP    MUSIC1       ; GO START INTERPRETING NEW SEGMENT

RFNCAL:  LDA    NOTES        ; REFRAIN CALL, SAVE CURRENT VALUE OF NOTES
         PHA                 ; ON THE STACK
         LDA    NOTES+1
         PHA
         JMP    NXTSEG       ; GO INTERPRET NEXT TWO ADDRESS BYTES

RFNRET:  PLA                 ; REFRAIN RETURN, RESTORE SAVED VALUE OF
         STA    NOTES+1      ; NOTES
         PLA
         STA    NOTES
         JSR    DINNOT       ; BUMP UP NOTES BY THREE SINCE IT WAS SAVED
         JSR    DINNOT       ; UNINCREMENTED
         JSR    DINNOT
         JMP    MUSIC1       ; GO INTERPRET NEXT EVENT

DINNOT:  INC    NOTES        ; DOUBLE INCREMENT NOTES POINTER SUBROUTINE
         BNE    DINN1
         INC    NOTES+1
DINN1:   RTS

;        4 VOICE PLAY SUBROUTINE
;        ENTER WITH VARIOUS TABLE POINTERS ALREADY SET UP
;        LOOPS TEMPO*DUR TIMES

PLAY:    LDY    #0           ; SET Y TO ZERO FOR STRAIGHT INDIRECT
         LDX    TEMPO        ; SET X TO TEMPO COUNT
                             ; COMPUTE AND OUTPUT A COMPOSITE SAMPLE
PLAY1:   CLC                 ; CLEAR CARRY
         LDA    (V1PT+1),Y   ; ADD UP 4 VOICE SAMPLES
         ADC    (V2PT+1),Y   ; USING INDIRECT ADDRESSING THROUGH VOICE
         ADC    (V3PT+1),Y   ; POINTERS INTO WAVEFORM TABLES
         ADC    (V4PT+1),Y   ; STRAIGHT INDIRECT WHEN Y INDEX = 0
         STA    $1700        ; SEND SUM TO DIGITAL-TO-ANALOG CONVERTER
         LDA    V1PT         ; ADD INCREMENTS TO POINTERS FOR
         ADC    V1IN         ; THE 4 VOICES
         STA    V1PT         ; FIRST FRACTIONAL PART
         LDA    V1PT+1
         ADC    V1IN+1
         STA    V1PT+1       ; THEN INTEGER PART
         LDA    V2PT         ; VOICE 2
         ADC    V2IN
         STA    V2PT
         LDA    V2PT+1
         ADC    V2IN+1
         STA    V2PT+1
         LDA    V3PT         ; VOICE 3
         ADC    V3IN
         STA    V3PT
         LDA    V3PT+1
         ADC    V3IN+1
         STA    V3PT+1
         LDA    V4PT         ; VOICE 4
         ADC    V4IN
         STA    V4PT
         LDA    V4PT+1
         ADC    V4IN+1
         STA    V4PT+1
         DEX                 ; DECREMENT & CHECK TEMPO COUNT
         BNE    TIMWAS       ; BRANCH TO TIME WASTE IF NOT RUN OUT
         DEC    DUR          ; DECREMENT & CHECK DURATION COUNTER
         BEQ    ENDNOT       ; JUMP OUT IF END OF NOTE
         LDX    TEMPO        ; RESTORE TEMPO COUNT
         BNE    PLAY1        ; CONTINUE PLAYING
TIMWAS:  BNE    TMWS1        ; 3  WASTE 12 STATES
TMWS1:   BNE    TMWS2        ; 3
TMWS2:   BNE    TMWS3        ; 3
TMWS3:   BNE    PLAY1        ; 3  CONTINUE PLAYING
ENDNOT:  RTS                 ; RETURN
                             ; TOTAL LOOP TIME = 114 STATES = 8770 HZ

P1END:                       ; DEFINE BEGINNING ADDRESS FOR THIRD PART
                             ; OF SONG TABLE

;        SONG TABLE

         .segment "PAGE2"    ; START SONG AT 0200

;        SONG TABLE FOR "EXODUS"
;        DURATION COUNT = 48 FOR QUARTER NOTE

                                          ; DUR  V1   V2   V3   V4
SONG:    .BYTE  2                         ; CALL THE MUSICAL SUBROUTINE
         .WORD  SUB1
         .BYTE  $0C,$22,$32,$3A,$3C       ; 1/16 E3   C4   E4   F4
         .BYTE  $18,$28,$32,$3A,$40       ; 1/8  G3   C4   E4   G4
         .BYTE  $18,$28,$32,$3A,$44       ; 1/8  G3   C4   E4   A4
         .BYTE  $24,$1E,$2C,$36,$3C       ; 1/8. D3   A3   D4   F4
         .BYTE  $0C,$1E,$2C,$00,$36       ; 1/16 D3   A3        D4
         .BYTE  $30,$22,$2C,$34,$3A       ; 1/4  E3   A3   C4#  E4
         .BYTE  $90,$14,$2C,$34,$3A       ; 1/2. A2   A3   C4#  E4
         .BYTE  2                         ; CALL THE SUBROUTINE AGAIN
         .WORD  SUB1
         .BYTE  $0C,$22,$32,$3A,$40       ; 1/16 C3   C4   E4   G4
         .BYTE  $18,$24,$2E,$36,$3C       ; 1/8  F3   B3@  D4   F4
         .BYTE  $18,$24,$2E,$36,$40       ; 1/8  F3   B3@  D4   G4
         .BYTE  $24,$1A,$28,$32,$3A       ; 1/8. C3   G3   C4   E4
         .BYTE  $0C,$00,$00,$00,$4A       ; 1/16                C5
         .BYTE  $18,$06,$36,$44,$4E       ; 1/8  D2   D4   A4   D5
         .BYTE  $18,$14,$36,$44,$4E       ; 1/8  A2   D4   A4   D5
         .BYTE  $18,$1E,$36,$44,$4E       ; 1/8  D3   D4   A4   D5
         .BYTE  $18,$22,$36,$44,$4E       ; 1/8  E3   D4   A4   D5
         .BYTE  $30,$24,$36,$44,$4E       ; 1/4  F3   D4   A4   D5
         .BYTE  $30,$24,$36,$44,$4E       ; 1/4  F3   D4   A4   D5
         .BYTE  $0C,$32,$3A,$44,$4A       ; 1/16 C4   E4   A4   C5
         .BYTE  $0C,$32,$3A,$00,$4E       ; 1/16 C4   E4        D5
         .BYTE  $0C,$32,$3A,$00,$4A       ; 1/16 C4   E4        C5
         .BYTE  $0C,$32,$3A,$00,$44       ; 1/16 C4   E4        A4
         .BYTE  $30,$28,$32,$40,$4A       ; 1/4  G3   C4   G4   C5
         .BYTE  $30,$14,$32,$40,$4A       ; 1/4  A2   C4   G4   C5
         .BYTE  $18,$2C,$32,$40,$4A       ; 1/8  A3   C4   G4   C5
         .BYTE  $18,$2C,$32,$40,$44       ; 1/8  A3   C4   G4   C5
         .BYTE  $18,$26,$36,$44,$4E       ; 1/8  F3#  D4   A4   D5
         .BYTE  $18,$26,$3A,$00,$52       ; 1/8  F3#  E4        E5
         .BYTE  $48,$24,$40,$4E,$58       ; 3/8  F3   G4   D5   G5
         .BYTE  $18,$24,$3C,$00,$54       ; 1/8  F3   F4        F5
         .BYTE  $18,$24,$3A,$00,$52       ; 1/8  F3   E4        E5
         .BYTE  $18,$24,$36,$00,$4E       ; 1/8  F3   D4        D5
         .BYTE  $0C,$32,$3A,$44,$4A       ; 1/16 C4   E4   A4   C5
         .BYTE  $0C,$32,$3A,$00,$4E       ; 1/16 C4   E4        D5
         .BYTE  $0C,$32,$3A,$00,$4A       ; 1/16 C4   E4        C5
         .BYTE  $0C,$32,$3A,$00,$44       ; 1/16 C4   E4        A4
         .BYTE  $30,$28,$32,$40,$4A       ; 1/4  G3   C4   G4   C5
         .BYTE  $30,$14,$32,$40,$4A       ; 1/4  A2   C4   G4   C5
         .BYTE  $18,$2C,$32,$40,$4A       ; 1/8  A3   C4   G4   C5
         .BYTE  $18,$2C,$32,$40,$44       ; 1/8  A3   C4   G4   A4
         .BYTE  $18,$2C,$3E,$44,$4E       ; 1/8  A3   F4#  A4   D5
         .BYTE  $18,$2C,$3E,$44,$52       ; 1/8  A3   F4#  A4   E5
         .BYTE  $48,$3C,$44,$52,$5C       ; 1/4. F4   A4   E5   A5
         .BYTE  $18,$40,$44,$00,$58       ; 1/8  G4   A4        G5
         .BYTE  $24,$3A,$44,$00,$52       ; 1/8. E4             E5
         .BYTE  $0C,$36,$44,$00,$4E       ; 1/16 D4   A4        D5
         .BYTE  $30,$2C,$44,$4C,$52       ; 1/4  A3   A4   C5#  E5
         .BYTE  $60,$14,$44,$4C,$52       ; 1/2, A2   A4   C5#  E5
         .BYTE  0                         ; END OF PIECE
;        THIS IS A "MUSICAL SUBROUTINE" WHICH CONTAINS A PASSAGE THAT IS
;        USED TWICE IN THE MAINLINE "EXODUS"

         .zeropage                        ; START SUBROUTINE IN
                                          ; REMAINDER OF PAGE 0

                                          ; DUR  V1   V2   V3   V4
SUB1:    .BYTE  $1C,$00,$2C,$00,$00       ; 1/8.      A3
         .BYTE  $48,$14,$24,$2C,$36       ; 1/4. A2   F3   A3   D4
         .BYTE  $18,$00,$00,$00,$44       ; 1/8                 A4
         .BYTE  $48,$1E,$30,$36,$40       ; 1/4. D3   B3   D4   G4
         .BYTE  $18,$00,$36,$00,$00       ; 1/8       D4
         .BYTE  $18,$24,$2E,$36,$3C       ; 1/8  F3   B3@  D4   F4
         .BYTE  $18,$00,$00,$00,$40       ; 1/8                 G4
         .BYTE  $24,$1A,$28,$32,$3A       ; 1/8. C3   G3   C4   E4
         .BYTE  $0C,$00,$00,$00,$32       ; 1/16                C4
         .BYTE  $48,$1E,$24,$2C,$36       ; 1/4. D3   F3   A3   D4
         .BYTE  $18,$00,$00,$00,$44       ; 1/8                 A4
         .BYTE  $18,$14,$32,$3A,$4A       ; 1/8  A2   C4   E4   C5
         .BYTE  $18,$22,$32,$3A,$4A       ; 1/8  E3   C4   E4   C5
         .BYTE  $18,$2C,$32,$3A,$4A       ; 1/8  A3   C4   E4   C5
         .BYTE  $18,$00,$32,$3A,$48       ; 1/8       C4   E4   B4
         .BYTE  $18,$2C,$32,$3C,$4A       ; 1/8  A3   C4   F4   C5
         .BYTE  $18,$2C,$32,$3C,$4E       ; 1/8  A3   C4   F4   D5
         .BYTE  $24,$28,$30,$36,$48       ; 1/8. G3   B3   D4   B4
         .BYTE  $0C,$28,$30,$36,$40       ; 1/16 G3   B3   D4   G4
         .BYTE  $30,$22,$34,$3A,$44       ; 1/4  E3   C4#  E4   A4
         .BYTE  1                         ; DEFINE BEGINNING OF NEXT
         .WORD  P1END                     ; SEGMENT AT END OF PAGE 1

         .segment "PAGE1"

         .BYTE  $30,$14,$34,$3A,$44       ; 1/4  A2   C4#  E4   A4
         .BYTE  $18,$14,$00,$00,$00       ; 1/8  A2
         .BYTE  $18,$14,$44,$00,$00       ; 1/8  A2   A4
         .BYTE  $18,$22,$32,$3A,$4A       ; 1/8  E3   C4   E4   C5
         .BYTE  1                         ; DEFINE BEGINNING OF NEXT
         .WORD  AUXRAM                    ; SEGMENT IN 6530 RAM

         .segment "AUXRAM"

         .BYTE  $18,$22,$32,$3A,$52       ; 1/8  E3   C4   E4   E5
         .BYTE  $24,$1E,$36,$2E,$4E       ; 1/8. D3   D4   F4#  D5
         .BYTE  $0C,$1E,$36,$2E,$44       ; 1/16 D3   D4   F4#  A4
         .BYTE  $18,$1E,$36,$2E,$44       ; 1/8  D3   D4   F4#  A4
         .BYTE  $18,$1E,$2C,$2E,$44       ; 1/8  D3   A3   F4#  A4
         .BYTE  $18,$1E,$2C,$2E,$44       ; 1/8  D3   A3   F4#  A4
         .BYTE  $18,$1E,$2C,$2E,$4E       ; 1/8  D3   A3   F4#  D5
         .BYTE  $18,$1E,$36,$2C,$44       ; 1/8  D3   D4   F4   A4
         .BYTE  $18,$1E,$36,$2C,$4E       ; 1/8  D3   D4   F4   D5
         .BYTE  $24,$22,$32,$3A,$52       ; 1/8. E3   C4   E4   E5
         .BYTE  $0C,$22,$32,$3A,$44       ; 1/16 E3   C4   E4   A4
         .BYTE  $18,$22,$32,$3A,$44       ; 1/8  E3   C4   E4   A4
         .BYTE  $18,$22,$2C,$3A,$44       ; 1/8  E3   A3   E4   A4
         .BYTE  $30,$22,$2C,$3A,$44       ; 1/4  E3   A3   E4   A4
         .BYTE  $18,$1A,$32,$3A,$40       ; 1/8  C3   C4   E4   G4
         .BYTE  $18,$1A,$32,$3A,$44       ; 1/8  C3   C4   E4   A4
         .BYTE  $18,$1E,$36,$40,$46       ; 1/8  D3   D4   G4   B4@
         .BYTE  $18,$1E,$36,$40,$4A       ; 1/8  D3   D4   G4   C5
         .BYTE  $24,$22,$32,$3A,$44       ; 1/8. E3   C4   E4   A4
         .BYTE  3                         ; RETURN

;        WAVEFORM TABLE
;        EXACTLY ONE PAGE LONG ON A PAGE BOUNDARY
;        MAXIMUM VALUE OF AN ENTRY IS 63 DECIMAL OR 3F HEX TO AVOID
;        OVERFLOW WHEN 4 VOICES ARE ADDED UP

         .segment "PAGE3"    ; START WAVEFORM TABLE AT 0300

WAV1TB:                      ; VOICE 1 WAVEFORM TABLE
WAV2TB:                      ; VOICE 2 WAVEFORM TABLE
WAV3TB:                      ; VOICE 3 WAVEFORM TABLE
WAV4TB:                      ; VOICE 4 WAVEFORM TABLE
                             ; NOTE THAT ALL 4 VOICES USE THIS TABLE DUE
                             ; TO LACK OF RAM IN BASIC KIM-1

;          FUNDAMENTAL AMPLITUDE  1.0 (REFERENCE)
;          SECOND HARMONIC .5, IN PHASE WITH FUNDAMENTAL
;          THIRD HARMONIC .5, 90 DEGREES LEADING PHASE

WAVXTB:  .BYTE  $33,$34,$35,$36,$36,$37,$38,$39
         .BYTE  $39,$3A,$3A,$3B,$3B,$3B,$3C,$3C
         .BYTE  $3C,$3C,$3C,$3C,$3C,$3C,$3C,$3C
         .BYTE  $3C,$3C,$3C,$3B,$3B,$3B,$3B,$3B
         .BYTE  $3A,$3A,$3A,$3A,$3A,$3A,$39,$39
         .BYTE  $39,$39,$39,$39,$39,$39,$39,$39
         .BYTE  $3A,$3A,$3A,$3A,$3A,$3B,$3B,$3B
         .BYTE  $3B,$3C,$3C,$3C,$3D,$3D,$3D,$3D
         .BYTE  $3E,$3E,$3E,$3E,$3F,$3F,$3F,$3F
         .BYTE  $3F,$3F,$3F,$3F,$3F,$3F,$3F,$3F
         .BYTE  $3E,$3E,$3E,$3D,$3D,$3C,$3C,$3B
         .BYTE  $3B,$3A,$39,$38,$38,$37,$36,$35
         .BYTE  $34,$33,$32,$31,$30,$2F,$2E,$2D
         .BYTE  $2C,$2B,$2A,$29,$28,$27,$26,$25
         .BYTE  $24,$23,$22,$21,$21,$20,$1F,$1F
         .BYTE  $1E,$1E,$1D,$1D,$1D,$1D,$1C,$1C
         .BYTE  $1C,$1C,$1D,$1D,$1D,$1D,$1D,$1E
         .BYTE  $1E,$1F,$1F,$20,$20,$21,$21,$22
         .BYTE  $23,$23,$24,$24,$25,$26,$26,$27
         .BYTE  $28,$28,$29,$29,$29,$2A,$2A,$2B
         .BYTE  $2B,$2B,$2B,$2B,$2B,$2B,$2B,$2A
         .BYTE  $2A,$2A,$29,$29,$28,$27,$27,$26
         .BYTE  $25,$24,$23,$22,$21,$20,$1F,$1D
         .BYTE  $1C,$1B,$19,$18,$17,$15,$14,$13
         .BYTE  $11,$10,$0F,$0D,$0C,$0B,$09,$08
         .BYTE  $07,$06,$05,$04,$03,$03,$02,$01
         .BYTE  $01,$00,$00,$00,$00,$00,$00,$00
         .BYTE  $00,$00,$01,$01,$01,$02,$03,$04
         .BYTE  $05,$06,$07,$08,$09,$0B,$0C,$0D
         .BYTE  $0F,$10,$12,$13,$15,$16,$18,$1A
         .BYTE  $1B,$1D,$1F,$20,$22,$23,$25,$27
         .BYTE  $28,$2A,$2B,$2C,$2E,$2F,$30,$31

         .END