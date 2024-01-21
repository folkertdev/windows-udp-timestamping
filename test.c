#include <stdio.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define SIO_TIMESTAMPING 2550137067u

#define TIMESTAMPING_FLAG_RX 1u;
#define TIMESTAMPING_FLAG_TX 2u;

typedef struct _TIMESTAMPING_CONFIG {
  ULONG  Flags;
  USHORT TxTimestampsBuffered;
} TIMESTAMPING_CONFIG, *PTIMESTAMPING_CONFIG;

int main() {
    DWORD numBytes;
    TIMESTAMPING_CONFIG config = { 0 };

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (serverSocket == INVALID_SOCKET) {
        fprintf(stderr, "Error creating socket: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Configure tx timestamp reception.
    config.Flags |= TIMESTAMPING_FLAG_TX;
    config.TxTimestampsBuffered = 1;
    int error =
        WSAIoctl(
            serverSocket,
            SIO_TIMESTAMPING,
            &config,
            sizeof(config),
            NULL,
            0,
            &numBytes,
            NULL,
            NULL);
    if (error == SOCKET_ERROR) {
        printf("WSAIoctl failed %d\n", WSAGetLastError());
        return -1;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(12345);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        fprintf(stderr, "Bind failed with error: %d\n", WSAGetLastError());
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    printf("UDP Server is listening on port 12345...\n");

    while (1) {
        char buffer[1024];
        struct sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        int bytesReceived = recvfrom(serverSocket, buffer, sizeof(buffer), 0, (struct sockaddr*)&clientAddr, &clientAddrLen);

        if (bytesReceived == SOCKET_ERROR) {
            fprintf(stderr, "recvfrom failed with error: %d\n", WSAGetLastError());
            continue;
        }

        // Process the received data and timestamps

        buffer[bytesReceived] = '\0';
        printf("Received from %s:%d - %s\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), buffer);
    }

    closesocket(serverSocket);
    WSACleanup();

    return 0;
}
