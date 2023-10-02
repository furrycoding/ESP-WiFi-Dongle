#include <stdlib.h>
#include <memory.h>

#include "dumb_serial.h"

#ifndef min
#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
      __typeof__ (b) _b = (b); \
      _a > _b ? _b : _a; })
#endif

#ifndef max
#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
      __typeof__ (b) _b = (b); \
      _a > _b ? _a : _b; })
#endif


#define FRAME_START ((uint8_t)0xE6)
#define FRAME_END ((uint8_t)0xE9)
#define ESC ((uint8_t)0xDB)
#define ESC_END ((uint8_t)0xDC)
#define ESC_ESC ((uint8_t)0xDD)
#define ESC_START ((uint8_t)0xDE)


#define OUTPUT_BYTE(s, x) if (s->outBufferPtr < s->outBufferSize) { s->outBuffer[s->outBufferPtr++] = x; }

// Reading

void end_chunk(read_state_t* s);

struct read_state_struct {
    uint8_t* outBuffer;
    size_t outBufferPtr, outBufferSize;
	size_t lastEndPtr;
	
    size_t chunkPtr;
    uint8_t chunk[9];

    uint8_t skipDetectBit;
    uint8_t skipCnt;
    size_t skipIndex;

    uint8_t isEscaping;
    uint8_t isData;
};


read_state_t* init_read_state(uint8_t* outBuffer, size_t outBufferSize) {
    read_state_t* ret = malloc(sizeof(read_state_t));
    memset(ret, 0, sizeof(read_state_t));
    ret->outBuffer = outBuffer;
    ret->outBufferSize = outBufferSize;
    return ret;
}

void deinit_read_state(read_state_t* s) {
    free(s);
}

size_t read_reset_buffer(read_state_t* s) {
	// TODO: Do something about this being called while we're in the middle of reading a packet
    size_t ret = s->outBufferPtr;
    s->lastEndPtr = 0;
	s->outBufferPtr = 0;
    return ret;
}

size_t read_process_byte(read_state_t* s, uint8_t byte) {
    if (byte == FRAME_START) {
		// In case the previous frame wasn't properly terminated, and we're still reading it
		s->outBufferPtr = s->lastEndPtr;
        s->skipDetectBit = 0;
        s->chunkPtr = 0;
        s->isEscaping = 0;
        s->skipIndex = 0;
        s->isData = 1;
		return NOT_COMPLETE_FRAME_START;
    } else if (byte == FRAME_END) {
		if (!s->isData)
			return IGNORED_FRAME_END;
		
        end_chunk(s);
        s->isData = 0;
		s->lastEndPtr = s->outBufferPtr;
        return s->outBufferPtr;
    } else if (!s->isData) {
        return NOT_DATA;
    }
	
	
	if (byte == ESC) {
		// For duplicate ESC bytes, simply ignore them
        s->isEscaping = 1;
        return NOT_COMPLETE;
    }
	
    if (s->isEscaping) {
        s->isEscaping = 0;
        switch(byte) {
            case ESC_END:
                byte = FRAME_END;
                break;
            case ESC_ESC:
                byte = ESC;
                break;
            case ESC_START:
                byte = FRAME_START;
                break;
            default:
                // Frame is likely gonna be corrupted...
                byte = byte;
                break;
        }
    }

    uint8_t upperBit = (byte >> 7) & 1;
    byte &= 0x7F;
    if (upperBit != s->skipDetectBit) {
        s->skipIndex = s->chunkPtr;
		s->chunk[s->chunkPtr++] = 0;
        s->skipCnt++;
    }
	s->skipDetectBit = upperBit ^ 1;

    int addLater = (s->chunkPtr >= 9);
    if (!addLater)
        s->chunk[s->chunkPtr++] = byte;
    
    if (s->chunkPtr >= 9) {
        end_chunk(s);
    }

    if (addLater)
        s->chunk[s->chunkPtr++] = byte;
    
    return NOT_COMPLETE;
}

void end_chunk(read_state_t* s) {
	uint8_t skipCnt = s->skipCnt;
    size_t l = s->chunkPtr;
    s->skipCnt = 0;
    s->chunkPtr = 0;
	
    if (l < 3)
		return;
	
    if (skipCnt == 1) {
        // If there's more than one skip - we can't correct it
        // If there are 0 skips - we don't need to correct anything

        // The error(or rather erasure) correction itself is simple
        // Since we know which byte is missing, and since we know that(thanks to the last parity byte) XOR of all bytes together should be 0
        // The result of the same XOR on our partial data would directly give us the missing byte
        uint8_t parity = 0;
        for (size_t i = 0; i < l; i++)
            parity ^= s->chunk[i];
        s->chunk[s->skipIndex] = parity;
    }
    
    // Since the highest bit is used for skip detection, we have to move it somewhere else
    // That somewhere is the second to last byte in the chunk
    uint8_t upper_bits = s->chunk[l - 2];
    for (size_t i = 0; i < (l - 2); i++) {
        uint8_t b = s->chunk[i];
        upper_bits <<= 1;
        b |= upper_bits & 0x80;
        OUTPUT_BYTE(s, b);
    }
}



// Writing

struct write_state_struct {
    uint8_t* outBuffer;
    size_t outBufferPtr, outBufferSize;

    size_t chunkPtr;
    uint8_t chunk[9];

    uint8_t skipDetectBit;
	uint8_t frameStarted;
};

write_state_t* init_write_state(uint8_t* outBuffer, size_t outBufferSize) {
    write_state_t* ret = malloc(sizeof(write_state_t));
    memset(ret, 0, sizeof(write_state_t));
    ret->outBuffer = outBuffer;
    ret->outBufferSize = outBufferSize;
    return ret;
}

void deinit_write_state(write_state_t* s) {
    free(s);
}

size_t write_reset_buffer(write_state_t* s) {
    size_t ret = s->outBufferPtr;
    s->outBufferPtr = 0;
    return ret;
}

size_t write_process_frame_chunk(write_state_t* s) {
    size_t cnt = min(7, s->chunkPtr);
	uint8_t* ptr = s->chunk;
	s->chunkPtr = 0;
	
    uint8_t chunk[9];
	
	uint8_t flipBit = s->skipDetectBit;
    uint8_t upper_bits = 0;
    for (size_t i = 0; i < cnt; i++) {
        uint8_t b = ptr[i];
        upper_bits |= (b & 0x80) >> (i + 1);
        
        chunk[i] = (b & 0x7F) | flipBit;
		flipBit ^= 0x80;
    }

    chunk[cnt] = upper_bits | flipBit;
	flipBit ^= 0x80;

    uint8_t parity = 0;
    for (size_t i = 0; i < (cnt + 1); i++)
        parity ^= chunk[i];
    chunk[cnt+1] = (parity & 0x7F) | flipBit;
	s->skipDetectBit = flipBit ^ 0x80;
	
    for (size_t i = 0; i < (cnt + 2); i++) {
        uint8_t b = chunk[i];
		
		// Check for special bytes
        if (b == FRAME_START) {
            b = ESC_START;
        } else if (b == FRAME_END) {
            b = ESC_END;
        } else if (b == ESC) {
            b = ESC_ESC;
        } else {
			// No need to escape, output as is
            OUTPUT_BYTE(s, b);
			continue;
        }
		
		// Escape that byte
		OUTPUT_BYTE(s, ESC);
		OUTPUT_BYTE(s, b);
    }

    return cnt;
}

void write_process_bytes(write_state_t* s, const uint8_t* b, size_t cnt) {
	if (!s->frameStarted) {
		s->skipDetectBit = 0;
		OUTPUT_BYTE(s, FRAME_START);
		s->frameStarted = 1;
	}
	
	while (cnt > 0) {
		if (s->chunkPtr >= 7)
			write_process_frame_chunk(s);
		s->chunk[s->chunkPtr++] = *(b++);
		cnt--;
	}
}

size_t write_end_frame(write_state_t* s) {
	if (s->frameStarted) {
		if (s->chunkPtr > 0)
			write_process_frame_chunk(s);
		OUTPUT_BYTE(s, FRAME_END);
		s->frameStarted = 0;
	}
	
	return s->outBufferPtr;
}
