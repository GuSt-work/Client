#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <iostream>
#include <QDebug>
#include <fstream>
#include "protocol.h"
#include<thread>
#include<filesystem>


int ParsePortUDP(const char* udpArg){
    int udpPort;
    try {
        udpPort = std::stoi(udpArg);
        if(udpPort < 0 || udpPort > 65535)
            return -1;
    }
    catch(...) {
        std::cerr << "Invalid UDP port" << "\n";
        return -1;
    }
    return udpPort;
}

std::string FormMessage(const char* udp, const char* fileName){
    int udpPort = ParsePortUDP(udp);
    std::string message = std::string(fileName) + ";" + std::to_string(udpPort) + "\n";
    return message;
}

void SendFileInfo(SOCKET sock, const char* udp, const char* fileName){
    auto message = FormMessage(udp, fileName);

    int totalSent = 0;
    int messLenght = message.size();

    while(totalSent < messLenght){
        int sent = send(sock, message.c_str() + totalSent, messLenght - totalSent, 0);
        if(sent == SOCKET_ERROR){
            std::cerr << "Failed send MEssage" << WSAGetLastError() << "\n";
            return;
        }
        totalSent += sent;
    }
    std::cout << "File info sent: " << message << std::endl;
}
bool CheckTime(const std::chrono::steady_clock::time_point &timePt, const int &timeoutMs){
    if(timePt == std::chrono::steady_clock::time_point{})
        return true;

    auto now = std::chrono::steady_clock::now();
    bool result = (std::chrono::duration_cast<std::chrono::milliseconds>(now - timePt).count() > timeoutMs);
    return result;
}

bool ParseMessage(SOCKET &serv_sock, std::string& buffer, uint32_t& id){
    char tmp[128];

    int received = recv(serv_sock, tmp, sizeof(tmp), 0);

    if (received == SOCKET_ERROR) {
        int err = WSAGetLastError();

        if (err != WSAEWOULDBLOCK) {
            std::cerr << "recvfrom error: " << err <<"\n";
            return false;
        }
    }
    else{
        buffer.append(tmp, received);
    }

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

bool IsReadyServer(SOCKET &serv_sock){
    uint32_t id;
    std::string buffer;

    const int TIMEOUT_MS = 5000;
    auto start = std::chrono::steady_clock::now();
    bool isReady = false;

    while (!isReady) {
        isReady = ParseMessage(serv_sock, buffer, id);

        if (isReady) return true;

        if (CheckTime(start, TIMEOUT_MS)) {
            std::cerr << "Server did not respond within timeout\n";
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

bool ReadFile(const char *filePath, std::vector<char>& data){
    std::ifstream file(filePath, std::ios::binary);

    if(!file){
        std::cerr << "Failed to open file" << "\n";
        return false;
    }
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();

    if (size > 10 * 1024 * 1024) {
        std::cerr << "File is very large" << "\n";
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



bool SendAll(SOCKET &sock, const std::string& message) {
    int totalSent = 0;
    int length = message.size();

    while (totalSent < length) {
        int sent = send(sock, message.c_str() + totalSent, length - totalSent, 0);

        if (sent == SOCKET_ERROR) {
            std::cerr << "Failed send Message" << message << WSAGetLastError() << "\n";
            return false;
        }

        totalSent += sent;
    }
    std::cout << message << std::endl;
    return true;
}

bool SendPackets(SOCKET &sockTCP, char *ip, char *portNumber, std::vector<PacketState> &states, int timeout){


    SOCKET sockUDP = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sockUDP == INVALID_SOCKET){
        std::cerr << "UDP socket creation failed" << "\n";
        return INVALID_SOCKET;
    }

    sockaddr_in addrServ;
    addrServ.sin_family = AF_INET;
    addrServ.sin_addr.s_addr = inet_addr(ip);
    addrServ.sin_port = htons((u_short)std::stoi(portNumber));

    int count = 0;
    std::string tcpBuffer;
    bool allAcked = false;

    while(true){
        for(auto &state : states){
            if(state.acknowledged)
                continue;

            if(!CheckTime(state.lastSend, timeout))
                continue;

            sendto(sockUDP, (char*)&state.pkt, sizeof(uint32_t)*2 + state.pkt.size, 0, (sockaddr*)&addrServ, sizeof(addrServ));
            state.lastSend = std::chrono::steady_clock::now();
        }

        uint32_t ackId;
        bool parceMes = ParseMessage(sockTCP, tcpBuffer, ackId);
        if(parceMes && ackId < states.size())
        {
            std::cout << "Packet " << ackId << " Is Come!" << std::endl;
            states[ackId].acknowledged = true;
        }
        if(!parceMes){
            int err = WSAGetLastError();
            if(err == WSAECONNRESET || err == WSAENOTSOCK){
                std::cerr << "Connection closed by server\n";
                break;
            }
        }

        allAcked = true;
        for(auto &state : states){
            if(!state.acknowledged){
                allAcked = false;
                break;
            }
        }
        if(allAcked){
            SendAll(sockTCP, "FIN\n");
            break;
        }
    }
    closesocket(sockUDP);
    return true;
}

bool ValidateArgs(int argc, char* argv[]){
    if(argc != 6){
        std::cerr << "[ERROR] Invalid number of arguments\n";
        return false;
    }

    sockaddr_in sa;
    if(inet_pton(AF_INET, argv[1], &(sa.sin_addr)) != 1){
        std::cerr << "[ERROR] Invalid IP address\n";
        return false;
    }

    for(int i = 2; i < 4; i++){
        int port = 0;
        try{
            port = std::stoi((argv[i]));
        } catch(...){
            std::cerr << "[ERROR] Port is not a number\n";
            return false;
        }
        if(port <=0 || port > 65535){
            std::cerr << "[ERROR] Invalid port \n";
            return false;
        }
    }

    std::filesystem::path dir(argv[4]);
    if(dir.empty()){
        std::cerr << "[ERROR] File path is empty\n";
        return false;
    }
    if (!std::filesystem::exists(dir)) {
        std::cerr << "[ERROR] Fale is not exist\n";
        return false;
    }

    try {
        int timeout = std::stoi(argv[5]);
        if (timeout <= 0) {
            std::cerr << "[ERROR] Invalid timeout value\n";
            return false;
        }
    } catch (const std::exception &e) {
        std::cerr << "[ERROR] Invalid timeout value\n";
        return false;
    }
    return true;
}

int main(int argc, char *argv[])
{
    if(!ValidateArgs(argc, argv))
        return -1;

    WSADATA wsa;
    if(WSAStartup(MAKEWORD(2,2), &wsa) != 0){
        std::cerr << "WSAStartUp failed\n";
        return -1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sock == INVALID_SOCKET){
        std::cerr << "Socket creation failed\n";
        WSACleanup();
        return -1;
    }

    sockaddr_in addrServ;
    addrServ.sin_family = AF_INET;
    addrServ.sin_addr.s_addr = inet_addr(argv[1]);
    addrServ.sin_port = htons((u_short)std::stoi(argv[2]));

    if(connect(sock, (sockaddr*)&addrServ, sizeof(addrServ)) != 0){
        int err = WSAGetLastError();
        std::cerr << "Client connect failed " << err << "\n";
        closesocket(sock);
        WSACleanup();
        return -1;
    }
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);

    SendFileInfo(sock, argv[3], argv[4]);

    bool isReadyServ = IsReadyServer(sock);
    if(!isReadyServ){
        closesocket(sock);
        WSACleanup();
        return -1;
    }

    std::cout << "UDP socket ready" << std::endl;
    std::vector<char> myFile;
    bool isReadFile = ReadFile(argv[4], myFile);
    if(!isReadFile){
        SendAll(sock, "FIN\n");
        closesocket(sock);
        WSACleanup();
        return -1;
    }

    std::vector<Packet> packets;
    CreateAllPackets(myFile, packets);

    std::vector<PacketState> states;
    CreatePacketsState(packets, states);
    std::cout << "File split into " << packets.size() << " packets" << std::endl;

    SendPackets(sock, argv[1], argv[3], states, ParsePortUDP(argv[5]));

    closesocket(sock);
    WSACleanup();
}
