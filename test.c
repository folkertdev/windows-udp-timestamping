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

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        fprintf(stderr, "Error creating socket: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    DWORD numBytes;
    TIMESTAMPING_CONFIG config = { 0 };
    // Configure tx timestamp reception.
    config.Flags |= TIMESTAMPING_FLAG_RX;
    config.TxTimestampsBuffered = 1;
    int error =
        WSAIoctl(
            udpSocket,
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

    struct sockaddr_in localAddress;
    localAddress.sin_family = AF_INET;
    localAddress.sin_addr.s_addr = INADDR_ANY;
    localAddress.sin_port = htons(12345);

    if (bind(udpSocket, (struct sockaddr*)&localAddress, sizeof(localAddress)) == SOCKET_ERROR) {
        fprintf(stderr, "Failed to bind socket.\n");
        closesocket(udpSocket);
        WSACleanup();
        return 1;
    }

    char buffer[1024];
    struct sockaddr_in senderAddress;
    int senderAddressSize = sizeof(senderAddress);
    struct sockaddr* pSenderAddr = (struct sockaddr*)&senderAddress;
    WSABUF dataBuffer;
    dataBuffer.buf = buffer;
    dataBuffer.len = sizeof(buffer);
    DWORD flags = 0;
    FILETIME timestamp;
    unsigned int c = 0;

    int bytesReceived = WSARecvFrom(
        udpSocket,
        &dataBuffer,
        1,
        &c,
        &flags,
        pSenderAddr,
        &senderAddressSize,
        NULL,
        NULL
    );

    if (bytesReceived == SOCKET_ERROR) {
        fprintf(stderr, "Error receiving data: %d\n", WSAGetLastError());
    } else {
        printf("made it very far");
//        if (WSAIoctl(udpSocket, SIO_TIMESTAMP_REQUEST, NULL, 0, &timestamp, sizeof(timestamp), NULL, NULL, NULL) == SOCKET_ERROR) {
//            fprintf(stderr, "Failed to extract timestamp: %d\n", WSAGetLastError());
//        } else {
//            printf("Received %d bytes from %s:%d at timestamp %llu\n", bytesReceived,
//                   inet_ntoa(senderAddress.sin_addr), ntohs(senderAddress.sin_port),
//                   ((unsigned long long)(timestamp.dwHighDateTime) << 32 | timestamp.dwLowDateTime));
//        }
    }

    // cleanup

    closesocket(udpSocket);
    WSACleanup();

    return 0;
}
