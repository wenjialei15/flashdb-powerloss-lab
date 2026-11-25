FLASHDB ?= third_party/FlashDB
CC      ?= cc

CFLAGS  ?= -std=c99 -O1 -g -Wall
CFLAGS  += -DFAL_PRINTF=sim_noop_printf

INCLUDES := -I$(FLASHDB)/inc \
            -I$(FLASHDB)/port/fal/inc \
            -Iport \
            -Isrc

FDB_SRCS := $(FLASHDB)/src/fdb.c \
            $(FLASHDB)/src/fdb_kvdb.c \
            $(FLASHDB)/src/fdb_tsdb.c \
            $(FLASHDB)/src/fdb_utils.c \
            $(FLASHDB)/src/fdb_file.c \
            $(FLASHDB)/src/fdb_read_cache.c

FAL_SRCS := $(FLASHDB)/port/fal/src/fal.c \
            $(FLASHDB)/port/fal/src/fal_flash.c \
            $(FLASHDB)/port/fal/src/fal_partition.c

LOCAL_SRCS := port/sim_flash.c src/main.c

SRCS := $(FDB_SRCS) $(FAL_SRCS) $(LOCAL_SRCS)

BUILD  := build
TARGET := $(BUILD)/powerloss-lab

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRCS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRCS) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD)
