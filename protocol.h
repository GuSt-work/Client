#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <cstdint>
#include<chrono>

const size_t CHUNK_SIZE = 1024;

#pragma pack(push, 1)
struct Packet {
    uint32_t id;
    uint32_t size;
    char data[CHUNK_SIZE];
};
#pragma pack(pop)

struct PacketState {
    Packet pkt;
    bool acknowledged = false;
    std::chrono::steady_clock::time_point lastSend = std::chrono::steady_clock::time_point{};
};


#endif // PROTOCOL_H
