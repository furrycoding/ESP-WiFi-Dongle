#define size_t uint32_t
#include "dumb_serial.h"

class PacketFraming {
public:
    PacketFraming();
    ~PacketFraming();

    // Returned pointer - array of bytes of length outputLength
    // Valid until next call to make_frame
    uint8_t* make_frame(uint8_t* data, uint16_t dataLength, uint8_t address, uint16_t localPort, uint16_t remotePort, size_t* outputLength);
    
    // status is an output value
    // meaning: 0 = byte is not part of a frame, -1 = frame is not complete yet, -2 = crc error, 1 = frame complete
    // returned pointer, address, port and outputLength are only valid if status = 1
    // Returned pointer - array of bytes of length outputLength
    // Valid until next call to make_frame
    uint8_t* parse_frame(uint8_t next_byte, int8_t* status, uint8_t* address, uint16_t* localPort, uint16_t* remotePort, uint16_t* outputLength);

private:
    void write(const uint8_t* data, size_t len);
    void read(uint8_t* data, size_t len);

    uint8_t preambleScanIdx;
    uint8_t* readBuffer;
};
