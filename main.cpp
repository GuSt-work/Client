#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <iostream>
#include <QDebug>
#include <chrono>
#include <thread>


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
    }
    else{

    }
    std::cout << "CONNECT" << "\n";
    SendFileInfo(sock, argv[3], argv[4]);
}
