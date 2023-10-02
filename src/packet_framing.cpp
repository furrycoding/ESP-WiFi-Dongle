#include "packet_framing.h"

#include <algorithm>
#include <memory.h>

#include <cstdio>


#define BUFFER_SIZE ((size_t)512)


// Source: https://github.com/esp8266/Arduino/blob/master/cores/esp8266/crc32.cpp
#define CRC16_POLY 0x5935
static void update_crc16(uint8_t* data, size_t length, uint16_t* crc) {
    uint16_t cur = *crc;
    while (length--)
    {
        uint8_t c = *(data++);
        for (uint32_t i = 0x80; i > 0; i >>= 1)
        {
            bool bit = cur & 0x8000;
            if (c & i)
                bit = !bit;
            cur <<= 1;
            if (bit)
                cur ^= CRC16_POLY;
        }
    }
    *crc = cur;
}

#define WRITE_AND_CRC(data, length, crc) \
    write(((uint8_t*)data), length); update_crc16(((uint8_t*)data), length, &crc)
#define READ_AND_CRC(data, length, crc) \
    read(((uint8_t*)data), length); update_crc16(((uint8_t*)data), length, &crc)





PacketFraming::PacketFraming() {
    readBuffer = new uint8_t[BUFFER_SIZE];
    writeBuffer = new uint8_t[BUFFER_SIZE];
    readState = init_read_state(readBuffer, BUFFER_SIZE);
    writeState = init_write_state(writeBuffer, BUFFER_SIZE);
}

PacketFraming::~PacketFraming() {
    deinit_read_state(readState);
    deinit_write_state(writeState);
    delete readBuffer;
    delete writeBuffer;
}


uint8_t* PacketFraming::make_frame(uint8_t* data, uint16_t dataLength, uint8_t address, uint16_t localPort, uint16_t remotePort, size_t* outputLength) {
    write_reset_buffer(writeState);

    uint16_t crc = 0;
    WRITE_AND_CRC(&dataLength, 2, crc);
    WRITE_AND_CRC(&address, 1, crc);
    WRITE_AND_CRC(&localPort, 2, crc);
    WRITE_AND_CRC(&remotePort, 2, crc);
    WRITE_AND_CRC(data, dataLength, crc);
    write((uint8_t*)&crc, 2);
    auto len = write_end_frame(writeState);

    *outputLength = len;
    return writeBuffer;
}

uint8_t* PacketFraming::parse_frame(uint8_t next_byte, int8_t* status, uint8_t* address, uint16_t* localPort, uint16_t* remotePort, uint16_t* outputLength) {
    auto len = read_process_byte(readState, next_byte);
    if (len == NOT_DATA) {
        *status = 0;
        return NULL;
    } else if (len == NOT_COMPLETE) {
        *status = -1;
        return NULL;
    }

    read_reset_buffer(readState);
    readLimit = std::min(len, BUFFER_SIZE);
    readPtr = 0;

    uint16_t crc = 0;
    
    uint16_t frameLen = 0;
    READ_AND_CRC(&frameLen, 2, crc);
    READ_AND_CRC(address, 1, crc);
    READ_AND_CRC(localPort, 2, crc);
    READ_AND_CRC(remotePort, 2, crc);

    auto data = &readBuffer[readPtr];
    auto end = std::min(readLimit, readPtr + frameLen);
    frameLen = end - readPtr;
    update_crc16(data, frameLen, &crc);
    readPtr = end;

    uint16_t frameCrc = 0;
    read((uint8_t*)&frameCrc, 2);

    if (crc != frameCrc) {
        *status = -2;
        
        
        printf("[DEBUG] Expected CRC %04hX, Got: %04hX\n", crc, frameCrc);
        printf("[DEBUG] Read buffer:\n");
        for (size_t i = 0; i < readLimit; i++)
            printf("%02hhX ", readBuffer[i]);
        printf("\n\n\n");

        return NULL;
    }

    *status = 1;
    *outputLength = frameLen;
    return data;
}

void PacketFraming::write(uint8_t* data, size_t len) {
    write_process_bytes(writeState, data, len);
}

void PacketFraming::read(uint8_t* data, size_t len) {
    auto end = std::min(readPtr+len, readLimit);
    
    if (end > readPtr)
        memcpy(data, &readBuffer[readPtr], end - readPtr);
    
    readPtr = end;
}
