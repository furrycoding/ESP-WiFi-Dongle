#ifndef _DUMB_SERIAL_H_
#define _DUMB_SERIAL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct read_state_struct read_state_t;
typedef struct write_state_struct write_state_t;

// read_process_byte special return values:
// Byte consumed, but state not changed
#define IGNORED_FRAME_END ((size_t) -4)
// Byte not consumed
#define NOT_DATA ((size_t) -3)
// New frame started, keep feeding bytes
#define NOT_COMPLETE_FRAME_START ((size_t) -2)
// Keep feeding bytes
#define NOT_COMPLETE ((size_t) -1)

read_state_t* init_read_state(uint8_t* outBuffer, size_t outBufferSize);
void deinit_read_state(read_state_t* s);
size_t read_reset_buffer(read_state_t* s);
size_t read_process_byte(read_state_t* s, uint8_t byte);

write_state_t* init_write_state(uint8_t* outBuffer, size_t outBufferSize);
void deinit_write_state(write_state_t* s);
size_t write_reset_buffer(write_state_t* s);
void write_process_bytes(write_state_t* s, const uint8_t* b, size_t cnt);
size_t write_end_frame(write_state_t* s);

#ifdef __cplusplus
}
#endif

#endif
