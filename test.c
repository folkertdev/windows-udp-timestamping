#include <stdio.h>
#include <winsock2.h>
#include <mswsock.h>
#include <mstcpip.h>
#include <ws2def.h>
#include <iphlpapi.h>

#pragma comment(lib, "ws2_32.lib")

#define SIO_TIMESTAMPING 2550137067u
#define SO_TIMESTAMP 0x300A

#define TIMESTAMPING_FLAG_RX 1u;
#define TIMESTAMPING_FLAG_TX 2u;

typedef struct _TIMESTAMPING_CONFIG {
  ULONG  Flags;
  USHORT TxTimestampsBuffered;
} TIMESTAMPING_CONFIG, *PTIMESTAMPING_CONFIG;

typedef struct _INTERFACE_HARDWARE_CROSSTIMESTAMP {
  ULONG64 SystemTimestamp1;
  ULONG64 HardwareClockTimestamp;
  ULONG64 SystemTimestamp2;
} INTERFACE_HARDWARE_CROSSTIMESTAMP, *PINTERFACE_HARDWARE_CROSSTIMESTAMP;

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
    // TODO what does this do?
    BOOL hardwareTimestampSource = 0;

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

    struct sockaddr_in localAddress;
    localAddress.sin_family = AF_INET;
    localAddress.sin_addr.s_addr = INADDR_ANY;
    localAddress.sin_port = htons(12345);

    int error;
    DWORD numBytes;
    int enableTimestamp = 1;
    if (setsockopt(udpSocket, SOL_SOCKET, SO_TIMESTAMP, (char*)&enableTimestamp, sizeof(enableTimestamp)) == SOCKET_ERROR) {
        printf("setsockopt SO_TIMESTAMP failed\n");
        // Handle the error
    }

    if (bind(udpSocket, (struct sockaddr*)&localAddress, sizeof(localAddress)) == SOCKET_ERROR) {
        fprintf(stderr, "Failed to bind socket.\n");
        closesocket(udpSocket);
        WSACleanup();
        return 1;
    }


//    TIMESTAMPING_CONFIG config = { 0 };
//    // Configure tx timestamp reception.
//    config.Flags |= TIMESTAMPING_FLAG_RX;
//    config.TxTimestampsBuffered = 5;
//    int error =
//        WSAIoctl(
//            udpSocket,
//            SIO_TIMESTAMPING,
//            &config,
//            sizeof(config),
//            NULL,
//            0,
//            &numBytes,
//            NULL,
//            NULL);
//    if (error == SOCKET_ERROR) {
//        printf("WSAIoctl failed %d\n", WSAGetLastError());
//        return -1;
//    }

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

    ULONG64 appLevelTimestamp;

    // Capture app-layer timestamp upon message reception.
    if (hardwareTimestampSource) {
//        INTERFACE_HARDWARE_CROSSTIMESTAMP crossTimestamp = { 0 };
//        crossTimestamp.Version = INTERFACE_HARDWARE_CROSSTIMESTAMP_VERSION_1;
//        error = CaptureInterfaceHardwareCrossTimestamp(interfaceLuid, &crossTimestamp);
//        if (error != NO_ERROR) {
//            printf("CaptureInterfaceHardwareCrossTimestamp failed %d\n", error);
//            return;
//        }
//        appLevelTimestamp = crossTimestamp.HardwareClockTimestamp;
    }
    else { // software source
        LARGE_INTEGER t1;
        QueryPerformanceCounter(&t1);
        appLevelTimestamp = t1.QuadPart;
    }

    // Look for socket rx timestamp returned via control message.
    BOOLEAN retrievedTimestamp = FALSE;
    WSACMSGHDR *cmsg = WSA_CMSG_FIRSTHDR(&wsaMsg);
    UINT64 socketTimestamp = 0;

    while (cmsg != NULL) {
        printf("cmsg level = %d, type = %d\n", cmsg->cmsg_level, cmsg->cmsg_type);
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMP) {
            printf("cmsg_len = %d\n", cmsg->cmsg_len);
            int *ptr = (int *)cmsg;
            printf("word %u\n", *ptr++);
            printf("word %u\n", *ptr++);
            printf("word %u\n", *ptr++);
            printf("word %u\n", *ptr++);
            printf("word %u\n", *ptr++);
            printf("word %u\n", *ptr++);
            socketTimestamp = *(PUINT64)WSA_CMSG_DATA(cmsg);
            printf("socket timestamp %lu\n", socketTimestamp);

            FILETIME* timestamp = (FILETIME*)WSA_CMSG_DATA(cmsg);
            printf("low timestamp %lu\n", timestamp->dwLowDateTime);
            printf("high timestamp %lu\n", timestamp->dwHighDateTime);

            retrievedTimestamp = TRUE;
            // break;
        }
        cmsg = WSA_CMSG_NXTHDR(&wsaMsg, cmsg);
    }


    printf("got very far %d", retrievedTimestamp);


    if (retrievedTimestamp) {
        // Compute socket receive path latency.
        LARGE_INTEGER clockFrequency;
        ULONG64 elapsedMicroseconds;

        if (hardwareTimestampSource) {
            // QueryHardwareClockFrequency(&clockFrequency);
        } else { // software source
            QueryPerformanceFrequency(&clockFrequency);
        }

        // Compute socket send path latency.
        elapsedMicroseconds = appLevelTimestamp - socketTimestamp;
        elapsedMicroseconds *= 1000000;
        elapsedMicroseconds /= clockFrequency.QuadPart;
        printf("RX latency estimation: %lld microseconds\n",
            elapsedMicroseconds);
    }
    else {
        printf("failed to retrieve RX timestamp\n");
    }

    // cleanup

    closesocket(udpSocket);
    WSACleanup();

    return 0;
}
