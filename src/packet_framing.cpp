#include "packet_framing.h"

#include <algorithm>
#include <memory.h>

#include <cstdio>

#include <Arduino.h>


#define BUFFER_SIZE ((size_t)512)
static const uint8_t PREAMBLE[] = {0xCF, 0xEB, 0x01, 0x81};


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
    preambleScanIdx = 0;
}

PacketFraming::~PacketFraming() {
    delete readBuffer;
}


uint8_t* PacketFraming::make_frame(uint8_t* data, uint16_t dataLength, uint8_t address, uint16_t localPort, uint16_t remotePort, size_t* outputLength) {
    uint16_t crc = 0;
    write(PREAMBLE, sizeof(PREAMBLE));
    WRITE_AND_CRC(&dataLength, 2, crc);
    WRITE_AND_CRC(&address, 1, crc);
    WRITE_AND_CRC(&localPort, 2, crc);
    WRITE_AND_CRC(&remotePort, 2, crc);
    WRITE_AND_CRC(data, dataLength, crc);
    write((uint8_t*)&crc, 2);
    
    // Already wrote everything to serial
    *outputLength = 0;
    return NULL;
}

uint8_t* PacketFraming::parse_frame(uint8_t next_byte, int8_t* status, uint8_t* address, uint16_t* localPort, uint16_t* remotePort, uint16_t* outputLength) {
    if (next_byte != PREAMBLE[preambleScanIdx]) {
        // If preambleScanIdx > 0 that means we already consumed a few bytes
        // ideally we would have a way to return them back to the serial stream
        // But I doubt this is ever going to be an actual problem since potential commands will be made up of ASCII characters
        preambleScanIdx = 0;
        *status = 0;
        return NULL;
    }

    preambleScanIdx++;
    if (preambleScanIdx < sizeof(PREAMBLE)) {
        *status = -1;
        return NULL;
    }
    
    // Preamble found!
    // Reset index for next scan
    preambleScanIdx = 0;

    // Now read the packet
    uint16_t crc = 0;
    
    uint16_t frameLen = 0;
    READ_AND_CRC(&frameLen, 2, crc);
    READ_AND_CRC(address, 1, crc);
    READ_AND_CRC(localPort, 2, crc);
    READ_AND_CRC(remotePort, 2, crc);

    frameLen = std::min((uint16_t)BUFFER_SIZE, frameLen);

    READ_AND_CRC(readBuffer, frameLen, crc);

    uint16_t frameCrc = 0;
    read((uint8_t*)&frameCrc, 2);

    if (crc != frameCrc) {
        *status = -2;
        
        
        printf("[DEBUG] Expected CRC %04hX, Got: %04hX\n", crc, frameCrc);
        printf("[DEBUG] Read buffer:\n");
        for (size_t i = 0; i < BUFFER_SIZE; i++)
            printf("%02hhX ", readBuffer[i]);
        printf("\n\n\n");

        return NULL;
    }

    *status = 1;
    *outputLength = frameLen;
    return readBuffer;
}

void PacketFraming::write(const uint8_t* data, size_t len) {
    Serial.write(data, len);
}

void PacketFraming::read(uint8_t* data, size_t len) {
   Serial.read(data, len);
}
