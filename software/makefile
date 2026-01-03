SYSRAM = 200
AUXRAM = 1780
EXTRAM = 2000
SONGA =  262F
WAVTBA = 3000

TARGETS = 01_kim4v.pap \
		  02_kim4v.pap \
		  03_kimfs.pap \
		  04_notint.pap \
		  05_dscore.pap \
		  06_dwaves.pap \
		  07_notcmp.pap \
		  08_notcmp.pap \
		  pcmplay.pap \
		  dscore.wav

INTERMEDIATES = dwaves.asm *.bin wav/*.bin wav/*.inc

MAKE = make
AS = ca65
LD = ld65
DD = dd
DDFLAGS = status=none
SREC_CAT = srec_cat
NOTCMP = utils/bin/notcmp
NOTINT = utils/bin/notint
WAVEGEN = utils/bin/wavegen
UTILS = $(NOTCMP) $(NOTINT) $(WAVEGEN)

# Default offset value
OFFSET = 0x0

.PHONY: all clean distclean
.SECONDARY: # Prevent deletion of intermediate files

# .PRECIOUS: %.asm
all: $(UTILS) $(TARGETS)

# Build utils rules

$(NOTCMP):
	@echo "Building NOTRAN Compiler Utility ($@)..."
	@$(MAKE) -C utils/notcmp

$(NOTINT):
	@echo "Building NOTRAN Interpreter Utility ($@)..."
	@$(MAKE) -C utils/notint

$(WAVEGEN):
	@echo "Building Waveform Generator Utility ($@)..."
	@$(MAKE) -C utils/wavegen

# Offset config rules
# We just define OFFSET for targets that differ from the default (0x0)
02_kim4v.pap:  OFFSET = 0x$(AUXRAM)
05_dscore.pap: OFFSET = 0x$(SONGA)
06_dwaves.pap: OFFSET = 0x$(WAVTBA)
08_notcmp.pap: OFFSET = 0x$(EXTRAM)

# Dependency rules
# We map which binary target depends on which .o object file and .cfg config file
01_0_kim4v.bin 01_1_kim4v.bin 01_2_kim4v.bin 02_kim4v.bin:  kim4v.o kim4v.cfg
03_0_kimfs.bin 03_1_kimfs.bin 03_2_kimfs.bin:				kimfs.o kimfs.cfg
04_0_notint.bin 04_2_notint.bin: 							notint.o notint.cfg
06_dwaves.bin:			     								dwaves.o dwaves.cfg
07_0_notcmp.bin 07_2_notcmp.bin 08_notcmp.bin: 				notcmp.o notcmp.cfg
P_pcmplay.bin D_pcmplay.bin: 								pcmplay.o pcmplay.cfg

# Absolute target rules

01_kim4v.pap: 01_0_kim4v.bin 01_1_kim4v.bin 01_2_kim4v.bin
	@echo "PAP $@"
	@$(SREC_CAT) 01_0_kim4v.bin -binary \
				 01_1_kim4v.bin -binary -offset 0x100 \
				 01_2_kim4v.bin -binary -offset 0x200 -o $@ -MOS_Technologies

03_kimfs.pap: 03_0_kimfs.bin 03_1_kimfs.bin 03_2_kimfs.bin
	@echo "PAP $@"
	@$(SREC_CAT) 03_0_kimfs.bin -binary \
				 03_1_kimfs.bin -binary -offset 0x100 \
				 03_2_kimfs.bin -binary -offset 0x200 -o $@ -MOS_Technologies

04_notint.pap: 04_0_notint.bin 04_2_notint.bin
	@echo "PAP $@"
	@$(SREC_CAT) 04_0_notint.bin -binary \
				 04_2_notint.bin -binary -offset 0x200 -o $@ -MOS_Technologies

07_notcmp.pap: 07_0_notcmp.bin 07_2_notcmp.bin
	@echo "PAP $@"
	@$(SREC_CAT) 07_0_notcmp.bin -binary \
				 07_2_notcmp.bin -binary -offset 0x200 -o $@ -MOS_Technologies

05_dscore.bin: dscore.not $(NOTCMP)
	@echo "NOT $@"
	@$(NOTCMP) $< -l $(basename $<).lst -o $@ -f bin

pcmplay.pap: P_pcmplay.bin D_pcmplay.bin
	@echo "PAP $@"
	@$(SREC_CAT) P_pcmplay.bin -binary -offset 0x$(SYSRAM) \
				 D_pcmplay.bin -binary -offset 0x$(EXTRAM) -o $@ -MOS_Technologies

pcmplay.o: pcmplay.asm wav/pcmdata.inc

wav/pcmdata.inc: wav/test.bin
	@echo "INC $@"
	@echo "; DO NOT EDIT - CHANGES WILL BE LOST AFTER MAKE!!\n;" > $@
	@echo '.segment "PCMDATA"\n\nPCM_TABLE:' >> $@
	@(hexdump -v -e '"    .byte " 15/1 "$$%02x," 1/1 "$$%02x\n"' | sed -e 's/,$$  //g') < $< >> $@
	@echo "TABLE_SIZE = * - PCM_TABLE" >> $@

dscore.wav: 05_dscore.bin 06_dwaves.bin $(NOTINT)
	@echo "INT $@"
	@$(NOTINT) -o $@ -j 10 $< 06_dwaves.bin

# Generic rules

wav/%.bin: wav/%.wav
	@echo "WAV2BIN $@"
	@$(DD) if=$< of=$@ bs=44 skip=1 $(DDFLAGS)

%.asm: %.yaml $(WAVEGEN)
	@echo "WAV $@"
	@$(WAVEGEN) $< -o $@

%.pap: %.bin
	@echo "PAP $@"
	@$(SREC_CAT) $< -binary -offset $(OFFSET) -o $@ -MOS_Technologies

%.bin:
	@echo "LD  $@"
	@$(LD) -C $(basename $<).cfg -vm -m $(basename $<).map -o $(basename $<).bin $<

%.o: %.asm
	@echo "AS  $@"
	@$(AS) -l $*.lst -o $@ $<

# Cleanup rules

clean:
	rm -f $(TARGETS) $(INTERMEDIATES) *.o *.lst *.map

distclean: clean
	@echo "Propagating 'clean' to utility subdirectories..."
	@$(MAKE) -C utils/notcmp clean
	@$(MAKE) -C utils/notint clean
	@$(MAKE) -C utils/wavegen clean
