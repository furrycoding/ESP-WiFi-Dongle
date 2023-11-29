#include <Arduino.h>
#include <Wire.h>

#include <vector>

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#include "defines.h"


#include "LEDManager.h"
#include "packet_framing.h"

LEDManager ledManager;


uint16_t targetPorts[] = {6969, 6970};

std::vector<WiFiUDP> Udps;
char incomingPacket[512];

PacketFraming framing;

#define LOG_EVERY_MS 5000

unsigned long nextLog = 0;
unsigned long looptimeCount = 0;
unsigned long looptimeStart = 0;

unsigned long wifi2serialCount = 0;
unsigned long serial2wifiCount = 0;

void halt() {
    ESP.deepSleep(0);
    while (true);
}

void setup()
{
    // halt();

    // Serial.begin(115200);
    Serial.begin(115200 * 10);
    Serial.println();
    Serial.println();
    Serial.println();
    ledManager.setUp();
    

    ledManager.setPattern(150, 5, 2);

    bool success = true;
    
    success &= WiFi.mode(WIFI_AP);

    // https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/generic-class.html
               WiFi.setOutputPower(17.5f);            // Higher Tx power can actually increase noise
    success &= WiFi.setPhyMode(WIFI_PHY_MODE_11N);    // Allegedly has highest indoor range
    success &= WiFi.setSleepMode(WIFI_NONE_SLEEP, 0); // We want lowest latency
    
    success &= WiFi.softAP(WIFI_SSID, WIFI_PASS, 1, WIFI_HIDDEN);

    if (!success) {
        printf("[!!!] Couldn't set up WiFi!\n");
        halt();
    }

    printf("Set up AP with SSID %s and pass length %d\n", WIFI_SSID, strlen(WIFI_PASS));
    printf("Running on %s\n", WiFi.softAPIP().toString().c_str());

    printf("Setting up UDP sockets\n");
    for (int i = 0; i < (sizeof(targetPorts)/sizeof(targetPorts[0])); i++) {
        WiFiUDP newUdp;
        newUdp.begin(targetPorts[i]);
        Udps.push_back(newUdp);
    }
    
    printf("Network setup done\n");

    
    ledManager.setPattern(1000, 3, 2);
    printf("Entering main loop\n");

    nextLog = millis();
    looptimeStart = micros();
    looptimeCount = 0;
}



void handle_serial_packet(uint8_t address, uint16_t localPort, uint16_t remotePort, uint8_t* data, uint16_t len) {
    WiFiUDP* udp = NULL;
    for (int i = 0; i < Udps.size(); i++)
        if (Udps[i].localPort() == localPort) {
            udp = &Udps[i];
            break;
        }
    
    if (udp == NULL)
        return;

    IPAddress addr(192, 168, 4, address);
    if (!udp->beginPacket(addr, remotePort)) {
        printf("[!!!] Error sending serial packet(begin) from port=%d; to: ip=%s, port=%d ; len=%d\n", localPort, addr.toString().c_str(), remotePort, len);
        return;
    }

    udp->write(data, len);

    if (!udp->endPacket()) {
        printf("[!!!] Error sending serial packet(end) from port=%d; to: ip=%s, port=%d ; len=%d\n", localPort, addr.toString().c_str(), remotePort, len);
        return;
    }
    
    //printf("Sent packet %d bytes long to %s:%d\n", len, addr.toString().c_str(), port);

    ledManager.activity();
}

int read_serial_char() {
    while (true) {
        auto next = Serial.read();
        if (next < 0)
            return next;
        
        auto byte = (uint8_t)next;

        int8_t status = 0;
        uint8_t address = 0;
        uint16_t localPort = 0;
        uint16_t remotePort = 0;
        uint16_t len = 0;
        uint8_t* ptr = framing.parse_frame(byte, &status, &address, &localPort, &remotePort, &len);

        if (status == -1)
            continue;

        if (status == 0)
            return byte;
        
        if (status != 1) {
            //printf("Wrong CRC for packet\n");
            // CRC or other error
            continue;
        }

        handle_serial_packet(address, localPort, remotePort, ptr, len);
        serial2wifiCount++;
    }
}

void update_serial2wifi() {
    while (Serial.available() > 0) {
        // TODO: Handle serial commands
        read_serial_char();
        optimistic_yield(100);
    }
}

void update_wifi2serial(WiFiUDP* udp) {
    bool activity = false;
    int packetLen;
    while ((packetLen = udp->parsePacket()) > 0) {
        auto ip = udp->remoteIP();
        uint8_t ipLowerByte = ip[3]; // This is the only byte that should actually change
        uint16_t localPort = udp->localPort();
        uint16_t remotePort = udp->remotePort();
        uint16_t writeLen = min(packetLen, (int)sizeof(incomingPacket));//packetLen;
        int len = udp->read(incomingPacket, writeLen);
        
        if (len != writeLen)
            printf("[!!!] Packet length mismatch: len=%d, writeLen=%d\n", len, writeLen);
        if (packetLen > (int)sizeof(incomingPacket))
            printf("[!] Packet truncated: packetLen=%d\n", packetLen);

        size_t frameLen = 0;
        uint8_t* ptr = framing.make_frame((uint8_t*)incomingPacket, writeLen, ipLowerByte, localPort, remotePort, &frameLen);

        if (ptr != NULL)
            Serial.write(ptr, frameLen);
        optimistic_yield(100);

        activity = true;
        wifi2serialCount++;
    }

    if (activity)
        ledManager.activity();
}

void loop()
{
    ledManager.update();
    
    update_serial2wifi();
    for (int i = 0; i < Udps.size(); i++)
        update_wifi2serial(&Udps[i]);
    looptimeCount++;

    if (millis() > nextLog) {
        nextLog = millis() + LOG_EVERY_MS;

        auto cur = micros();
        auto dt = cur - looptimeStart;
        looptimeStart = cur;
        auto cnt = looptimeCount;
        looptimeCount = 0;

        auto loopsPerSec = cnt * 1e6f / std::max(dt, 1ul);

        printf("[STATS] Average loops/sec: %f(count: %ld, micros: %ld)\n", loopsPerSec, cnt, dt);
        printf("[STATS] Packets sent since last log: WiFi->Serial: %ld ; Serial->WiFi: %ld\n", wifi2serialCount, serial2wifiCount);
        wifi2serialCount = serial2wifiCount = 0;
    }

    optimistic_yield(100);
    
    delay(1);
}
