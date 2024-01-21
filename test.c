#include <stdio.h>
#include <winsock2.h>
#include <mswsock.h>
#include <mstcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define SIO_TIMESTAMPING 2550137067u

#define TIMESTAMPING_FLAG_RX 1u;
#define TIMESTAMPING_FLAG_TX 2u;

typedef struct _TIMESTAMPING_CONFIG {
  ULONG  Flags;
  USHORT TxTimestampsBuffered;
} TIMESTAMPING_CONFIG, *PTIMESTAMPING_CONFIG;

static LPFN_WSARECVMSG getwsarecvmsg()
{
    LPFN_WSARECVMSG lpfnWSARecvMsg = NULL;
    GUID guidWSARecvMsg = WSAID_WSARECVMSG;
    SOCKET sock = INVALID_SOCKET;
    DWORD dwBytes = 0;
    sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (SOCKET_ERROR == WSAIoctl(sock,
                    SIO_GET_EXTENSION_FUNCTION_POINTER,
                    &guidWSARecvMsg,
                    sizeof(guidWSARecvMsg),
                    &lpfnWSARecvMsg,
                    sizeof(lpfnWSARecvMsg),
                    &dwBytes,
                    NULL,
                    NULL
            ))
    {
        return NULL;
    }
    // safe_close_soket(sock);
    return lpfnWSARecvMsg;
}


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

    LPFN_WSARECVMSG recvmsg = getwsarecvmsg();

    CHAR data[512];
    WSABUF dataBuf;
    CHAR control[WSA_CMSG_SPACE(sizeof(UINT64))] = { 0 };
    // CHAR control[512] = { 0 };
    WSAMSG wsaMsg;
    WSABUF controlBuf;

    dataBuf.buf = data;
    dataBuf.len = sizeof(data);
    controlBuf.buf = control;
    controlBuf.len = sizeof(control);
    wsaMsg.name = NULL;
    wsaMsg.namelen = 0;
    wsaMsg.lpBuffers = &dataBuf;
    wsaMsg.dwBufferCount = 1;
    wsaMsg.Control = controlBuf;
    wsaMsg.dwFlags = 0;

    error =
        recvmsg(
            udpSocket,
            &wsaMsg,
            &numBytes,
            NULL,
            NULL);
    if (error == SOCKET_ERROR) {
        printf("recvmsg failed %d\n", WSAGetLastError());
        return -1;
    }

    printf("got very far");

    // cleanup

    closesocket(udpSocket);
    WSACleanup();

    return 0;
}
