# Build path
BUILD_DIR = out
TARGET = test

CC = gcc
AS = gcc -x assembler-with-cpp
CP = objcopy
SZ = size

HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

# C sources
C_SOURCES =  \
    main.c   \
    bitmap.c  \


# C includes
C_INCLUDES =  \
    -I. \


CFLAGS = $(C_INCLUDES)
LDFLAGS = 

# list of objects
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
#$(warning OBJECTS=${OBJECTS})
vpath %.c $(sort $(dir $(C_SOURCES)))

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR) 
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile | $(BUILD_DIR)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(BIN) $< $@

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin

clean:
	rm -fr out/*
