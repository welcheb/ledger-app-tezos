#include "apdu.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

unsigned int handle_apdu_error(uint8_t instruction) {
    THROW(0x6D00);
}

unsigned int handle_apdu_exit(uint8_t instruction) {
    os_sched_exit(-1);
    THROW(0x6D00); // avoid warning
}

uint32_t send_word_big_endian(uint32_t word) {
    char word_bytes[sizeof(word)];
    memcpy(word_bytes, &word, sizeof(word));
    uint32_t tx = 0;
    for (; tx < sizeof(word); tx++) {
        G_io_apdu_buffer[tx] = word_bytes[sizeof(word) - tx - 1];
    }
    G_io_apdu_buffer[tx++] = 0x90;
    G_io_apdu_buffer[tx++] = 0x00;
    return tx;
}

unsigned int handle_apdu_version(uint8_t instruction) {
    const uint32_t VERSION = 0;
    return send_word_big_endian(VERSION);
}

#define CLA 0x80

void main_loop(apdu_handler handlers[INS_MASK + 1]) {
    volatile uint32_t rx = io_exchange(CHANNEL_APDU, 0);
    while (true) {
        BEGIN_TRY {
            TRY {
                if (rx == 0) {
                    // no apdu received, well, reset the session, and reset the
                    // bootloader configuration
                    THROW(0x6982);
                }

                if (G_io_apdu_buffer[0] != CLA) {
                    THROW(0x6E00);
                }

                // The amount of bytes we get in our APDU must match what the APDU declares
                // its own content length is. All these values are unsigned, so this implies
                // that if rx < OFFSET_CDATA it also throws.
                if (rx != G_io_apdu_buffer[OFFSET_LC] + OFFSET_CDATA) {
                    THROW(0x6D00);
                }

                uint8_t instruction = G_io_apdu_buffer[1];
                uint32_t tx = handlers[instruction & INS_MASK](instruction);
                rx = io_exchange(CHANNEL_APDU, tx);
            }
            CATCH(ASYNC_EXCEPTION) {
                rx = io_exchange(CHANNEL_APDU | IO_ASYNCH_REPLY, 0);
            }
            CATCH_OTHER(e) {
                unsigned short sw = e;
                switch (sw) {
                default:
                    sw = 0x6800 | (e & 0x7FF);
                    // FALL THROUGH
                case 0x6000 ... 0x6FFF:
                case 0x9000 ... 0x9FFF:
                    G_io_apdu_buffer[0] = sw >> 8;
                    G_io_apdu_buffer[1] = sw;
                    rx = io_exchange(CHANNEL_APDU, 2);
                    break;
                }
            }
            FINALLY {
            }
        }
        END_TRY;
    }
}

// I have no idea what this function does, but it is called by the OS
unsigned short io_exchange_al(unsigned char channel, unsigned short tx_len) {
    switch (channel & ~(IO_FLAGS)) {
    case CHANNEL_KEYBOARD:
        break;

    // multiplexed io exchange over a SPI channel and TLV encapsulated protocol
    case CHANNEL_SPI:
        if (tx_len) {
            io_seproxyhal_spi_send(G_io_apdu_buffer, tx_len);

            if (channel & IO_RESET_AFTER_REPLIED) {
                reset();
            }
            return 0; // nothing received from the master so far (it's a tx
                      // transaction)
        } else {
            return io_seproxyhal_spi_recv(G_io_apdu_buffer,
                                          sizeof(G_io_apdu_buffer), 0);
        }

    default:
        THROW(INVALID_PARAMETER);
    }
    return 0;
}
