/**
 +------------------------------------------------------------------------------
 * @desc        	: WebSocket C Server
 +------------------------------------------------------------------------------
 * @author      	: Bottle<bottle@fridayws.com>
 * @since       	: 16-05-11
 * @fileName    	: websocket.c
 * @version     	: 1.0
 +------------------------------------------------------------------------------
**/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <openssl/sha.h>
#include <openssl/buffer.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

// DEBUG control
/*#define __BT_DEBUG_*/

#define PORT 8000
#define MAXLENGTH 1024
#define MAXEVENTS 128
#define RECVBUFSIZE 2048

#define CLOSECLIENT 101 // close the client sockfd
#define SHOWALL 102 // send all client fd to request client
#define SHOWTHIS 103 // send this client fd to request client

// Type define
typedef unsigned char BYTE; // 定义一个BYTE类型
typedef unsigned short UINT16; // 定义一个UINT16类型
typedef unsigned long UINT64; // 定义一个UINT64类型

typedef struct _WebSocketMark {
    BYTE fin:1;
    BYTE rsv1:1;
    BYTE rsv2:1;
    BYTE rsv3:1;
    BYTE opcode:4;
    BYTE mask:1;
    BYTE payloadlen:7;
} WSMark;

typedef struct _WebSocketHeader {
    WSMark mark;
    UINT64 reallength;
    unsigned char mask[4];
    unsigned short headlength;
} WSHeader;

// Global variable define 
struct epoll_event* p_events = NULL;
int clients[1024] = {0};
size_t clientNum = 0;

typedef union test {
    unsigned char c[2];
    UINT16 i;
} test;

/**
 +------------------------------------------------------------------------------
 * @desc        	: 打印一个Addr的结构
 +------------------------------------------------------------------------------
 * @author      	: Bottle<bottle.friday@gmail.com>
 * @since       	: 2016-05-12
 * @param       	: int sockfd socket 标识符
 * @return      	: void
 +------------------------------------------------------------------------------
**/
void printSockAddr(int sockfd)
{
    struct sockaddr_in caddr;
    socklen_t addlen;
    getsockname(sockfd, (struct sockaddr *)&caddr, &addlen);
    printf("List SockAddr(SockFD: %d):\n", sockfd);
    printf("####################################################################\n");
    printf("Addr: %s:%d;\n", inet_ntoa(caddr.sin_addr), caddr.sin_port);
    printf("s_addr: %d;\n", caddr.sin_addr.s_addr);
    printf("####################################################################\n");
}

/**
 +------------------------------------------------------------------------------
 * @desc        	: 打印数据包头内容
 +------------------------------------------------------------------------------
 * @author      	: Bottle<bottle.friday@gmail.com>
 * @since       	: 2016-05-12
 * @param       	: const WSHeader* header 包头结构体
 * @return      	: void
 +------------------------------------------------------------------------------
**/
void printPackInfo(const WSHeader* pHeader)
{
    printf("fin: %d\nrsv1: %d\nrsv2: %d\nrsv3: %d\nopcode: %d\nmask: %d\npayloadleng_pre: %d\npayloadleng: %ld\nmask: %x, %x, %x, %x\nheadlength: %u\n",
           pHeader->mark.fin, pHeader->mark.rsv1, pHeader->mark.rsv2, pHeader->mark.rsv3, pHeader->mark.opcode, pHeader->mark.mask, pHeader->mark.payloadlen,
           pHeader->reallength, pHeader->mask[0], pHeader->mask[1], pHeader->mask[2], pHeader->mask[3], pHeader->headlength);
}

/**
 +------------------------------------------------------------------------------
 * @desc        	: 获取并返回一个sockfd带有的IPADDR
 +------------------------------------------------------------------------------
 * @author      	: Bottle<bottle.friday@gmail.com>
 * @since       	: 2016-05-12
 * @param       	: int sockfd socket标识符
 * @return      	: char* addr
 +------------------------------------------------------------------------------
**/
char* getSockAddr(int sockfd)
{
    struct sockaddr_in caddr;
    socklen_t addrlen;
    getsockname(sockfd, (struct sockaddr*)&caddr, &addrlen);
    return inet_ntoa(caddr.sin_addr);
}

/**
 +------------------------------------------------------------------------------
 * @desc        	: 获取消息体
 +------------------------------------------------------------------------------
 * @access	      	: public
 * @author	      	: bottle<bottle@fridayws.com>
 * @since       	: 16-05-17
 * @param       	: const int cfd client fd
 * @param           : const unsigned char* buf 消息buf
 * @param           : size_t bufsize 消息长度
 * @param           : unsigned char* container 获取到的消息体存放地址
 * @param           : const WSHeader* pHeader 头结构体地址
 * @return      	: int
 +------------------------------------------------------------------------------
**/
int getPackPayloadData(const int cfd, const unsigned char* buf, size_t bufsize, unsigned char* container, const WSHeader* pHeader)
{
    memset(container, 0, pHeader->reallength + 1);
    int readlength = 0;
    int recvlength = 0;
    int count = 0;
    char *_buf = (char*)calloc(bufsize, sizeof(char));
    if (pHeader->mark.mask) // 如果有掩码
    {
        readlength = bufsize - pHeader->headlength;
        int x = 0;
        memcpy(container, buf + pHeader->headlength, pHeader->reallength > readlength ? readlength : pHeader->reallength);
        while(pHeader->reallength > readlength)
        {
			memset(_buf, 0, bufsize);
            count = recv(cfd, _buf, bufsize, MSG_DONTWAIT);
            recvlength = (pHeader->reallength - readlength) > bufsize ? bufsize : (pHeader->reallength - readlength);
            memcpy(container + readlength, _buf, recvlength);
            readlength += recvlength;
        }
        for (x = 0; x < pHeader->reallength; ++x)
            *(container + x) ^= pHeader->mask[x % 4];
    }
    else
    {
        readlength = bufsize - pHeader->headlength;
        memcpy(container, buf + pHeader->headlength, pHeader->reallength > readlength ? readlength : pHeader->reallength);
        while(pHeader->reallength > readlength)
        {
			memset(_buf, 0, bufsize);
            count = recv(cfd, _buf, bufsize, MSG_DONTWAIT);
            recvlength = pHeader->reallength - readlength > bufsize ? bufsize : pHeader->reallength - readlength;
            memcpy(container + readlength, _buf, recvlength);
            readlength += recvlength;
        }
    }
    free(_buf);
    _buf = NULL;
    return 0;
}

/**
 +------------------------------------------------------------------------------
 * @desc        	: 解析接收到的数据包
 +------------------------------------------------------------------------------
 * @access	      	: public
 * @author	      	: bottle<bottle@fridayws.com>
 * @since       	: 16-05-11
 * @param       	: unsigned char* buf 接收到的数据内容
 * @param           : size_t length 接收的数据长度
 * @param           : WSHeader* 头部存放结构体
 * @return      	: int 成功返回0
 +------------------------------------------------------------------------------
**/
int parsePack(unsigned char* buf, size_t length, WSHeader* header)
{
    //memcpy(&(header->mark), buf, 2); // 直接得到前两个固定字节的信息
    header->mark.fin = buf[0] >> 7;
    header->mark.rsv1 = buf[0] & 0x40;
    header->mark.rsv2 = buf[0] & 0x20;
    header->mark.rsv3 = buf[0] & 0x10;
    header->mark.opcode = buf[0] & 0xF;
    header->mark.mask = buf[1] >> 7;
    header->mark.payloadlen = buf[1] & 0x7F;
    header->headlength = 2;
    header->reallength = header->mark.payloadlen;
    if (header->mark.payloadlen == 126)
    {
        UINT16 tmp16 = 0;
        memcpy(&tmp16, buf + 2, 2);
        header->reallength = ntohs(tmp16);
        header->headlength += 2;
    }
    else if (header->mark.payloadlen == 127)
    {
        UINT64 tmp64 = 0;
        memcpy(&tmp64, buf + 2, 8);
        header->reallength = ntohl(tmp64);
        header->headlength += 8;
    }
    memset(header->mask, 0, 4);
    if (header->mark.mask)
    {
        memcpy(header->mask, buf + header->headlength, 4);
        header->headlength += 4;
    }
    return 0;
}

/**
 +------------------------------------------------------------------------------
 * @desc        	: 对发送数据打包
 +------------------------------------------------------------------------------
 * @author      	: Bottle<bottle.friday@gmail.com>
 * @since       	: 2016-05-11
 * @param       	: const unsigned char* message 需要发送的消息体
 * @param           : size_t len 发送数据长度
 * @param           : BYTE fin 是否是结束消息 (1 bit)
 * @param           : BYTE opcode 消息类型(4 bit) 共15种类型
 * @param           : BYTE mask (是否需要做掩码运算 1 bit)
 * @param           : unsigned char** send 输出参数, 存放处理好的数据包
 * @param           : size_t* slen 输出参数, 记录数据包的长度
 * @return      	: int 成功返回0
 +------------------------------------------------------------------------------
**/
int packData(const unsigned char* message, size_t len, BYTE fin, BYTE opcode, BYTE mask, unsigned char** send, size_t* slen)
{
	int headLength = 0;
    // 基本一个包可以发送完所有数据
    *slen = len;
    if (len < 126) // 如果不需要扩展长度位, 两个字节存放 fin(1bit) + rsv[3](1bit) + opcode(4bit); mask(1bit) + payloadLength(7bit);
        *slen += 2;
    else if (len < 0xFFFF) // 如果数据长度超过126 并且小于两个字节, 我们再用后面的两个字节(16bit) 表示 UINT16
        *slen += 4;
    else // 如果数据更长的话, 我们使用后面的8个字节(64bit)表示 UINT64
        *slen += 8;
    // 判断是否有掩码
    if (mask & 0x1) // 判断是不是1
        *slen += 4; // 4byte 掩码位
    // 长度已确定, 现在可以重新分配内存
    *send = (unsigned char*)realloc((void*)*send, *slen);
    // 做数据设置
    memset(*send, 0, *slen);
    **send = fin << 7;
    **send = **send | (0xF & opcode); //处理opcode
    *(*send + 1) = mask << 7;
    if (len < 126)
    {
        *(*send + 1) = *(*send + 1) | len;
        //start += 2;
		headLength += 2;
    }
    else if (len < 0xFFFF)
    {
        *(*send + 1) = *(*send + 1) | 0x7E; // 设置第二个字节后7bit为126
        UINT16 tmp = htons((UINT16)len);
        //UINT16 tmp = len;
        memcpy(*send + 2, &tmp, sizeof(UINT16));
		headLength += 4;
    }
    else
    {
        *(*send + 1) = *(*send + 1) | 0x7F; // 设置第二个字节后为7bit 127
        UINT64 tmp = htonl((UINT64)len);
        //UINT64 tmp = len;
        memcpy(*send + 2, &tmp, sizeof(UINT64));
		headLength += 10;
    }
    // 处理掩码
    if (mask & 0x1)
    {
        // 因协议规定, 从服务器向客户端发送的数据, 一定不能使用掩码处理. 所以这边省略
		headLength += 4;
    }
    memcpy((*send) + headLength, message, len);
    *(*send + (*slen - 1)) = '\0';
    return 0;
}

/**
 +------------------------------------------------------------------------------
 * @desc        	: 指令控制
 +------------------------------------------------------------------------------
 * @access	      	: public
 * @author	      	: bottle<bottle@fridayws.com>
 * @since       	: 16-05-11
 * @param       	: char* buf 指令内容
 * @return      	: int 返回指令的标识
 +------------------------------------------------------------------------------
**/
int cmdctl(char* buf)
{
    char* head = "Cmd:";
    if(!strncmp(buf, head, 4))
    {
        char* cmd = buf + strlen(head);
        if (!strncmp(cmd, "quit", 4))
            return CLOSECLIENT;
        if (!strncmp(cmd, "showall", 7))
            return SHOWALL;
        if (!strncmp(cmd, "show", 4))
            return SHOWTHIS;
    }
    return 0;
}

/**
 +------------------------------------------------------------------------------
 * @desc        	: 处理指令
 +------------------------------------------------------------------------------
 * @access	      	: public
 * @author	      	: bottle<bottle@fridayws.com>
 * @since       	: 16-05-11
 * @param       	: unsigned int cmd 指令标识
 * @return      	: int
 +------------------------------------------------------------------------------
**/
int docmd(unsigned int cmd, int fd)
{
    switch (cmd)
    {
        int i = 0;
        case CLOSECLIENT:
            close(fd);
            for (i = 0; i < clientNum; ++i)
            {
                if (clients[i] == fd)
                    break;
            }
            for (i; i < clientNum; ++i)
                clients[i] = clients[i + 1];
            clients[i] = 0;
            --clientNum;
            break;
        case SHOWALL:
            printf("All clients: \n");
            for (i = 0; i < clientNum; ++i)
                printf("%d\n", clients[i]);
            break;
        case SHOWTHIS:
            printf("this Client: %d\n", fd);
            break;
        default:
            break;
    }
    return 0;
}

/**
 +------------------------------------------------------------------------------
 * @desc        	: 对数据做base64处理
 +------------------------------------------------------------------------------
 * @access	      	: public
 * @author	      	: bottle<bottle@fridayws.com>
 * @since       	: 16-05-11
 * @param       	: char* str 需要做base64的字符串
 * @param           : int len 数据长度
 * @param           : char* encode 处理好的数据存放位置
 * @param           : int 数据的实际长度
 * @return      	: int 长度
 +------------------------------------------------------------------------------
**/
int base64_encode(char* str, int len, char* encode, int elen)
{
    BIO* bmem, * b64;
    BUF_MEM* bptr = NULL;
    b64 = BIO_new(BIO_f_base64());
    bmem = BIO_new (BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, str, len);
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);

    elen = bptr->length;
    memcpy(encode, bptr->data, bptr->length);
	if (encode[elen - 1] == '\n' || encode[elen - 1] == '\r')
		encode[elen - 1] = '\0';
    BIO_free_all(b64);
    return elen;
}

/**
 +------------------------------------------------------------------------------
 * @desc        	: 反处理, 解析base64
 +------------------------------------------------------------------------------
 * @access	      	: public
 * @author	      	: bottle<bottle@fridayws.com>
 * @since       	: 16-05-11
 * @param       	: char* encode 已编码过的数据
 * @param           : int elen 编码过的数据长度
 * @param           : char* decode 存放解码后的数据
 * @param           : int dlen 存放解码后的数据长度
 * @return      	: void
 +------------------------------------------------------------------------------
**/
int base64_decode(char* encode, int elen, char* decode, int dlen)
{
    int len = 0;
    BIO* b64, * bmem;
    b64 = BIO_new(BIO_f_base64());
    bmem = BIO_new_mem_buf(encode, elen);
    bmem = BIO_push(b64, bmem);
    len = BIO_read(bmem, decode, elen);
    decode[len] = 0;
    BIO_free_all(bmem);
    return 0;
}

/**
 +------------------------------------------------------------------------------
 * @desc        	: 握手数据解析, 并返回校验数据
 +------------------------------------------------------------------------------
 * @access	      	: public
 * @author	      	: bottle<bottle@fridayws.com>
 * @since       	: 16-05-11
 * @param       	: const char* data 需要校验的数据
 * @param           : char* request 可发送加客户端的已处理数据
 * @return      	: 0
 +------------------------------------------------------------------------------
**/
int shakeHands(const char* data, char* request)
{
    char* key = "Sec-WebSocket-Key: ";
    char* begin = NULL;
    char* val = NULL;
    int needle = 0;
    begin = strstr(data, key);
    if (!begin)
        return -1;
    val = (char*)malloc(sizeof(char) * 256);
    memset(val, 0, 256);
    begin += strlen(key);
    unsigned int blen = strlen(begin);
    int i = 0;
    for (i = 0; i < blen; ++i)
    {
        if (*(begin + i) == '\r' && *(begin + i + 1) == '\n')
            break;
        *(val + i) = *(begin + i);
    }
    strcat(val, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    char mt[SHA_DIGEST_LENGTH] = {0};
    char accept[256] = {0};
    SHA1(val, strlen(val), mt);
	memset(accept, 0, 256);
    base64_encode(mt, strlen(mt), accept, 256);
    memset(request, 0, 1024);
    sprintf(request, "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %*s\r\n"
            "Sec-webSocket-Version: 13\r\n"
            "Server: Bottle-websocket-server\r\n\r\n"
			, strlen(accept), accept);
    free(val);
    val = NULL;
    return 0;
}

int main(int argc, char* argv[])
{
    int sockfd, len, maxfd, ret, retval, newfd, status = 0;
    int result = 1;
    struct sockaddr_in l_addr;
    struct sockaddr_in c_addr;
    struct timeval tv;
    char buf[MAXLENGTH] = {0};
    char request[MAXLENGTH] = {0};
    int epollFd = 0;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (0 > sockfd)
    {
        perror("socket:");
        status = -1;
        goto error0;
    }
    l_addr.sin_port = htons(PORT);
    l_addr.sin_family = AF_INET;
    l_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind (sockfd, (struct sockaddr*)&l_addr, sizeof(struct sockaddr)) < 0)
    {
        perror("bind:");
        status = -2;
        goto error1;
    }
    if (0 > listen(sockfd, 128))
    {
        perror("Listen:");
        status = -3;
        goto error1;
    }
    epollFd = epoll_create1(0);
    struct epoll_event event;
    event.data.fd = sockfd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollFd, EPOLL_CTL_ADD, sockfd, &event);
    p_events = calloc(MAXEVENTS, sizeof(event));
    len = sizeof(struct sockaddr);
    bzero(request, MAXLENGTH);
    //printf("Init...\nsockfd = %d\n", sockfd);
    int n = 0;
    char requestHeader[1024] = {0};
    for (;;) // to accept socket link
    {
        int i = 0;
        n = epoll_wait(epollFd, p_events, MAXEVENTS, -1);
        for (i = 0; i < n; ++i)
        {
            if (((p_events + i)->events & EPOLLERR) ||
                ((p_events + i)->events & EPOLLHUP) ||
                (!(p_events + i)->events & EPOLLIN))
            {
                // delete clientfd form epoll
                int j = 0;
                for (j = 0; j < clientNum; ++j)
                {
                    if (clients[j] == (p_events + i)->data.fd)
                    {
                        epoll_ctl(epollFd, EPOLL_CTL_DEL, clients[j], &event);
                        // delete clientFd from clients
                        clients[j] = 0;
                        break;
                    }
                }
                // move arr
                for (j; j < clientNum; ++j)
                    clients[j] = clients[j + 1];
                clients[j--] = 0;
            }
            else if (sockfd == (p_events + i)->data.fd) // connect
            {
                //printf("conn...\n");
                struct sockaddr_in clientAddr = {};
                socklen_t clientLen = 0;
                int clientFd = 0;
                if (-1 == (clientFd = accept(sockfd, (struct sockaddr*)&clientAddr, &clientLen)))
                {
                    perror("accept");
                    goto error1;
                }
                event.data.fd = clientFd;
                event.events = EPOLLIN | EPOLLET;
                epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &event);
                clients[clientNum++] = clientFd;
            }
            else
            {
                //printf("message...\n");
                ssize_t count = 0;
                char buf[RECVBUFSIZE] = {0};
                count = recv((p_events + i)->data.fd, buf, RECVBUFSIZE, MSG_DONTWAIT);
                //printSockAddr((p_events + i)->data.fd);
                if (0 == shakeHands(buf, requestHeader))  // 判断是否是连接请求
                {
                    send((p_events + i)->data.fd, requestHeader, strlen(requestHeader), MSG_DONTWAIT);
#ifdef __BT_DEBUG_
					printf("Request: ##################################\n%s\n", buf);
					printf("Request End ###############################\n");
					printf("-------------------------------------------\n");
					printf("Response: #################################\n");
					printf("%s\n", requestHeader);
					printf("Response End ##############################\n");
#endif
                    continue;
                }
                WSHeader header;
                parsePack(buf, RECVBUFSIZE, &header);
                unsigned char* container = (unsigned char*)malloc(header.reallength + 1);
                //memset(container, 0, header.reallength + 1);
                getPackPayloadData((p_events + i)->data.fd, buf, RECVBUFSIZE, container, &header);
                unsigned int cmd = 0;
                if (0 < (cmd = cmdctl(container)))
                    docmd(cmd, (p_events + i)->data.fd);
                unsigned char* _send = (char*)malloc(sizeof(unsigned char));
                size_t slen = 0;
                //printf("length: %ld\n", strlen(container));
                packData(container, header.reallength + 1, 0x1, 0x1, 0, &_send, &slen);
                // TODO test code start
                //WSHeader responseHeader;
                //parsePack(_send, slen, &responseHeader);
                //printPackInfo(&responseHeader);
                //printf("%s\n", _send + responseHeader.headlength);
                /*char* _container = (char*)malloc(responseHeader.reallength);
                memset(_container, 0, responseHeader.reallength);
                getPackPayloadData(0, _send, strlen(_send),  _container, &responseHeader);
                printf("preSendData: %s\n", _container);
                free(_container);*/
                //printf("%02x %02x\n", *(_send), *(_send + 1));
                // TODO test code end
                int k = 0;
                for (k = 0; k < clientNum; ++k)
                {
                    //if (clients[k] != (p_events + i)->data.fd)
                        send(clients[k], (void*)_send, slen, MSG_DONTWAIT);
                }
                free(container);
                free(_send);
                _send = NULL;
                container = NULL;
            }
        }
    }
    return 0;
    error2:
    ;
    error1:
    close(sockfd);
    if (!!newfd)
        close(newfd);
    error0:
    return status;
}
