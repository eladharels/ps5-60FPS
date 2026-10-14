PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := hello.elf
CFLAGS := -Wall -Werror -g

all: $(ELF)

$(ELF): main.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(ELF)

# Sends the ELF to the loader on the PS5 and streams its stdout back
test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^

.PHONY: all clean test
