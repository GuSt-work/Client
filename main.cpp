#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <iostream>
#include <QDebug>
#include <fstream>
#include "protocol.h"
#include<thread>


int GetUDP(const char* udpArg){
    int udpPort;
    try {
        udpPort = std::stoi(udpArg);
        if(udpPort < 0 || udpPort > 65535)
            return -1;
    }
    catch(...) {
        std::cerr << "Invalid UDP port\n";
        return -1;
    }
    return udpPort;
}

std::string GetMessage(const char* udp, const char* fileName){
    int udpPort = GetUDP(udp);
    std::string message = std::string(fileName) + ";" + std::to_string(udpPort) + "\n";
    return message;
}

void SendFileInfo(SOCKET sock, const char* udp, const char* fileName){
    auto message = GetMessage(udp, fileName);

    int totalSent = 0;
    int messLenght = message.size();

    while(totalSent < messLenght){
        int sent = send(sock, message.c_str() + totalSent, messLenght - totalSent, 0);
        if(sent == SOCKET_ERROR){
            std:: cout << "Failed send MEssage" << WSAGetLastError() << "\n";
            return;
        }
        totalSent += sent;
    }
    std::cout << "File info sent: " << message;
}

bool ParseMessage(SOCKET &serv_sock, std::string& buffer, uint32_t& id){
    char tmp[128];

    int received = recv(serv_sock, tmp, sizeof(tmp), 0);
    if(received <= 0){
        return false;
    }

    buffer.append(tmp, received);
    size_t pos = buffer.find('\n');

    if(pos != std::string::npos){
        std::string message = buffer.substr(0, pos);
        buffer.erase(0, pos+1);

        if (message.rfind("ACK ", 0) != 0)
            return false;

        id = std::stoul(message.substr(4));
        return true;
    }
    return false;
}

bool ReadFile(const char *filePath, std::vector<char>& data){
    std::ifstream file(filePath, std::ios::binary);

    if(!file){
        std::cout << "Failed to open file \n";
        return false;
    }
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();

    if (size > 10 * 1024 * 1024) {
        std::cout << "File very large\n";
        return false;
    }

    file.seekg(0, std::ios::beg);

    data.resize(size);
    file.read(data.data(), size);

    return true;
}

void CreateAllPackets(const std::vector<char>& data, std::vector<Packet> &packets){
    uint32_t id = 0;

    for (size_t i = 0; i < data.size(); i += CHUNK_SIZE) {
        Packet pkt{};
        pkt.id = id++;

        size_t chunk = std::min(CHUNK_SIZE, data.size() - i);
        pkt.size = chunk;

        memcpy(pkt.data, data.data() + i, chunk);

        packets.push_back(pkt);
    }
}

void CreatePacketsState(std::vector<Packet> &packets, std::vector<PacketState> &states){
    for (auto& pkt : packets) {
        PacketState state;
        state.pkt = pkt;
        states.push_back(state);
    }
}

bool CheckTime(const std::chrono::steady_clock::time_point &timePt, const int &timeoutMs){
    if(timePt == std::chrono::steady_clock::time_point{})
        return true;

    auto now = std::chrono::steady_clock::now();
    bool result = (std::chrono::duration_cast<std::chrono::milliseconds>(now - timePt).count() > timeoutMs);
    return result;
}

bool SendAll(SOCKET &sock, const std::string& message) {
    int totalSent = 0;
    int length = message.size();

    while (totalSent < length) {
        int sent = send(sock, message.c_str() + totalSent, length - totalSent, 0);

        if (sent == SOCKET_ERROR) {
            std:: cout << "Failed send MEssage" << WSAGetLastError() << "\n";
            return false;
        }

        totalSent += sent;
    }
    qDebug() << "Message send!";
    return true;
}

bool CreateUDP(SOCKET &sock_tcp, char *ip, char *portNumber, std::vector<PacketState> &states, int timeout){
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock == INVALID_SOCKET){
        std::cout << "CLIET: udp socket creation failed \n";
        return INVALID_SOCKET;
    }

    sockaddr_in addrServ;
    addrServ.sin_family = AF_INET;
    addrServ.sin_addr.s_addr = inet_addr(ip);
    addrServ.sin_port = htons((u_short)strtol(portNumber, NULL, 10));

    int count = 0;
    std::string tcpBuffer;
    bool allAcked = false;
    while(count <11){
        //++count;
        for(auto &state : states){
            if(state.acknowledged)
                continue;

            if(!CheckTime(state.lastSend, timeout))
                continue;

            sendto(sock, (char*)&state.pkt, sizeof(uint32_t)*2 + state.pkt.size, 0, (sockaddr*)&addrServ, sizeof(addrServ));
            state.lastSend = std::chrono::steady_clock::now();
        }

        uint32_t ackId;
        bool parceMes = ParseMessage(sock_tcp, tcpBuffer, ackId);
        if(parceMes && ackId < states.size())
        {
            std::cout << "Packet " << ackId << " Is Come! \n";
            qDebug() << "Packet " << ackId << " Is Come! \n";
            states[ackId].acknowledged = true;
        }

        allAcked = true;
        for(auto &state : states){
            if(!state.acknowledged){
                allAcked = false;
                break;
            }
        }
        if(allAcked){
            SendAll(sock_tcp, "FIN\n");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return true;
}

int main(int argc, char *argv[])
{
    WSADATA wsa;
    if(WSAStartup(MAKEWORD(2,2), &wsa) != 0){
        std::cout << "CLIET: WSAStartUp failed \n";
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sock == INVALID_SOCKET){
        std::cout << "CLIET: socket creation failed \n";
        WSACleanup();
        return 1;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(argv[1]);
    addr.sin_port = htons((u_short)strtol(argv[2], NULL, 10));

    //if(bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR){
        //std:: cout << "CLIET: bind setting error \n";
        //return 1;
    //}
    std::cout << "CLIET: ZABINDILI \n";

    sockaddr_in addrServ;
    addrServ.sin_family = AF_INET;
    addrServ.sin_addr.s_addr = inet_addr(argv[1]);
    addrServ.sin_port = htons((u_short)strtol(argv[2], NULL, 10));

    if(connect(sock, (sockaddr*)&addrServ, sizeof(addrServ)) != 0){
        int err = WSAGetLastError();
        std::cout << "CLIET: NO_CONNECT " << err << "\n";
        closesocket(sock);
        WSACleanup();
    }


    std::cout << "CONNECT" << "\n";
    SendFileInfo(sock, argv[3], argv[4]);


    uint32_t id;
    std::string buffer;
    bool isReadyServ = false;
    while(!isReadyServ){
        isReadyServ = ParseMessage(sock, buffer, id);
    }
    if(!isReadyServ){
        closesocket(sock);
        WSACleanup();
        return -1;
    }

    std::cout << "SERVER GETTING   " << id << "\n";
    std::vector<char> myFile;
    bool isReadFile = ReadFile(argv[4], myFile);
    if(!isReadFile){
        closesocket(sock);
        WSACleanup();
        return -1;
    }

    std::vector<Packet> packets;
    CreateAllPackets(myFile, packets);

    std::vector<PacketState> states;
    CreatePacketsState(packets, states);
    std::cout << "CreateUDP   " << "\n";
    bool isSendFile = CreateUDP(sock, argv[1], argv[3], states, GetUDP(argv[5]));

    closesocket(sock);
    WSACleanup();
}
