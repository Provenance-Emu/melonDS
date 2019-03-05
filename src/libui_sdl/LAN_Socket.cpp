/*
    Copyright 2016-2019 Arisotura

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

// indirect LAN interface, powered by BSD sockets.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Wifi.h"
#include "LAN_Socket.h"
#include "../Config.h"

#ifdef __WIN32__
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#define socket_t    SOCKET
	#define sockaddr_t  SOCKADDR
#else
	#include <unistd.h>
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/select.h>
	#include <sys/socket.h>
	#define socket_t    int
	#define sockaddr_t  struct sockaddr
	#define closesocket close
#endif

#ifndef INVALID_SOCKET
#define INVALID_SOCKET  (socket_t)-1
#endif


namespace LAN_Socket
{

const u32 kSubnet   = 0x0A400000;
const u32 kServerIP = kSubnet | 0x01;
const u32 kDNSIP    = kSubnet | 0x02;
const u32 kClientIP = kSubnet | 0x10;

const u8 kServerMAC[6] = {0x00, 0xAB, 0x33, 0x28, 0x99, 0x44};
const u8 kDNSMAC[6]    = {0x00, 0xAB, 0x33, 0x28, 0x99, 0x55};

u8 PacketBuffer[2048];
int PacketLen;
volatile int RXNum;

u16 IPv4ID;


// TODO: UDP sockets
// * use FIFO list
// * assign new socket when seeing new IP/port


typedef struct
{
    u8 DestIP[4];
    u16 DestPort;

    // 0: unused
    // 1: connected
    u8 Status;

    SOCKET Backend;

} TCPSocket;

TCPSocket TCPSocketList[16];


bool Init()
{
    // TODO: how to deal with cases where an adapter is unplugged or changes config??
    //if (PCapLib) return true;

    //Lib = NULL;
    PacketLen = 0;
    RXNum = 0;

    IPv4ID = 1;

    memset(TCPSocketList, 0, sizeof(TCPSocketList));

    return true;
}

void DeInit()
{
    //
}


void FinishUDPFrame(u8* data, int len)
{
    u8* ipheader = &data[0xE];
    u8* udpheader = &data[0x22];

    // lengths
    *(u16*)&ipheader[2] = htons(len - 0xE);
    *(u16*)&udpheader[4] = htons(len - (0xE + 0x14));

    // IP checksum
    u32 tmp = 0;

    for (int i = 0; i < 20; i += 2)
        tmp += ntohs(*(u16*)&ipheader[i]);
    while (tmp >> 16)
        tmp = (tmp & 0xFFFF) + (tmp >> 16);
    tmp ^= 0xFFFF;
    *(u16*)&ipheader[10] = htons(tmp);

    // UDP checksum
    // (note: normally not mandatory, but some older sgIP versions require it)
    tmp = 0;
    tmp += ntohs(*(u16*)&ipheader[12]);
    tmp += ntohs(*(u16*)&ipheader[14]);
    tmp += ntohs(*(u16*)&ipheader[16]);
    tmp += ntohs(*(u16*)&ipheader[18]);
    tmp += ntohs(0x1100);
    tmp += (len-0x22);
    for (u8* i = udpheader; i < &udpheader[len-0x22]; i += 2)
        tmp += ntohs(*(u16*)i);
    while (tmp >> 16)
        tmp = (tmp & 0xFFFF) + (tmp >> 16);
    tmp ^= 0xFFFF;
    if (tmp == 0) tmp = 0xFFFF;
    *(u16*)&udpheader[6] = htons(tmp);
}


/*bool HandleIncomingIPFrame(u8* data, int len)
{
    const u32 serverip = 0x0A404001;
    const u32 clientip = 0x0A404010;

    //if (memcmp(&data[0x1E], PCapAdapterData->IP_v4, 4))
    //    return false;

    u8 protocol = data[0x17];

    //memcpy(&data[6], &PCapAdapterData->DHCP_MAC[0], 6);
    memcpy(&data[0], Wifi::GetMAC(), 6);
    data[6] = 0x00; data[7] = 0xAB; data[8] = 0x33;
    data[9] = 0x28; data[10] = 0x99; data[11] = 0x44;

    *(u32*)&data[0x1E] = htonl(clientip);

    u8* ipheader = &data[0xE];
    u8* protoheader = &data[0x22];

    // IP checksum
    u32 tmp = 0;

    *(u16*)&ipheader[10] = 0;
    for (int i = 0; i < 20; i += 2)
        tmp += ntohs(*(u16*)&ipheader[i]);
    while (tmp >> 16)
        tmp = (tmp & 0xFFFF) + (tmp >> 16);
    tmp ^= 0xFFFF;
    *(u16*)&ipheader[10] = htons(tmp);

    if (protocol == 0x11)
    {
        u32 udplen = ntohs(*(u16*)&protoheader[4]);

        // UDP checksum
        tmp = 0;
        *(u16*)&protoheader[6] = 0;
        tmp += ntohs(*(u16*)&ipheader[12]);
        tmp += ntohs(*(u16*)&ipheader[14]);
        tmp += ntohs(*(u16*)&ipheader[16]);
        tmp += ntohs(*(u16*)&ipheader[18]);
        tmp += ntohs(0x1100);
        tmp += udplen;
        for (u8* i = protoheader; i < &protoheader[udplen-1]; i += 2)
            tmp += ntohs(*(u16*)i);
        if (udplen & 1) tmp += (protoheader[udplen-1] << 8);
        while (tmp >> 16)
            tmp = (tmp & 0xFFFF) + (tmp >> 16);
        tmp ^= 0xFFFF;
        if (tmp == 0) tmp = 0xFFFF;
        *(u16*)&protoheader[6] = htons(tmp);
    }
    else if (protocol == 0x06)
    {
        u32 tcplen = ntohs(*(u16*)&ipheader[2]) - 0x14;

        u16 srcport = ntohs(*(u16*)&protoheader[0]);
        u16 dstport = ntohs(*(u16*)&protoheader[2]);
        u16 flags = ntohs(*(u16*)&protoheader[12]);

        // TODO: check if they send a FIN, I guess
        int sockid = -1;
        for (int i = 0; i < (sizeof(TCPSocketList)/sizeof(TCPSocket)); i++)
        {
            TCPSocket* sock = &TCPSocketList[i];
            if (sock->Status == 1 && !memcmp(&sock->DestIP, &ipheader[12], 4) && sock->DestPort == srcport)
            {
                sockid = i;
                break;
            }
        }

        if (sockid == -1)
        {
            return true;
        }

        // TCP checksum
        tmp = 0;
        *(u16*)&protoheader[16] = 0;
        tmp += ntohs(*(u16*)&ipheader[12]);
        tmp += ntohs(*(u16*)&ipheader[14]);
        tmp += ntohs(*(u16*)&ipheader[16]);
        tmp += ntohs(*(u16*)&ipheader[18]);
        tmp += ntohs(0x0600);
        tmp += tcplen;
        for (u8* i = protoheader; i < &protoheader[tcplen-1]; i += 2)
            tmp += ntohs(*(u16*)i);
        if (tcplen & 1) tmp += (protoheader[tcplen-1] << 8);
        while (tmp >> 16)
            tmp = (tmp & 0xFFFF) + (tmp >> 16);
        tmp ^= 0xFFFF;
        *(u16*)&protoheader[16] = htons(tmp);
    }

    return false;
}*/

/*void RXCallback(u_char* blarg, const struct pcap_pkthdr* header, const u_char* data)
{
    while (PCapRXNum > 0);

    if (header->len > 2048-64) return;

    PCapPacketLen = header->len;
    memcpy(PCapPacketBuffer, data, PCapPacketLen);
    PCapRXNum = 1;

    if (!Config::DirectLAN)
    {
        u16 ethertype = ntohs(*(u16*)&data[0xC]);

        if (ethertype == 0x0800) // IPv4
        {
            if (HandleIncomingIPFrame(PCapPacketBuffer, header->len))
                PCapRXNum = 0;
        }
    }
}*/

void HandleDHCPFrame(u8* data, int len)
{
    u8 type = 0xFF;

    u32 transid = *(u32*)&data[0x2E];

    u8* options = &data[0x11A];
    for (;;)
    {
        if (options >= &data[len]) break;
        u8 opt = *options++;
        if (opt == 255) break;

        u8 len = *options++;
        switch (opt)
        {
        case 53: // frame type
            type = options[0];
            break;
        }

        options += len;
    }

    if (type == 0xFF)
    {
        printf("DHCP: bad frame\n");
        return;
    }

    printf("DHCP: frame type %d, transid %08X\n", type, transid);

    if (type == 1 || // discover
        type == 3)   // request
    {
        u8 resp[512];
        u8* out = &resp[0];

        // ethernet
        memcpy(out, &data[6], 6); out += 6;
        memcpy(out, kServerMAC, 6); out += 6;
        *(u16*)out = htons(0x0800); out += 2;

        // IP
        u8* ipheader = out;
        *out++ = 0x45;
        *out++ = 0x00;
        *(u16*)out = 0; out += 2; // total length
        *(u16*)out = htons(IPv4ID); out += 2; IPv4ID++;
        *out++ = 0x00;
        *out++ = 0x00;
        *out++ = 0x80; // TTL
        *out++ = 0x11; // protocol (UDP)
        *(u16*)out = 0; out += 2; // checksum
        *(u32*)out = htonl(kServerIP); out += 4; // source IP
        if (type == 1)
        {
            *(u32*)out = htonl(0xFFFFFFFF); out += 4; // destination IP
        }
        else if (type == 3)
        {
            *(u32*)out = htonl(kClientIP); out += 4; // destination IP
        }

        // UDP
        u8* udpheader = out;
        *(u16*)out = htons(67); out += 2; // source port
        *(u16*)out = htons(68); out += 2; // destination port
        *(u16*)out = 0; out += 2; // length
        *(u16*)out = 0; out += 2; // checksum

        // DHCP
        u8* body = out;
        *out++ = 0x02;
        *out++ = 0x01;
        *out++ = 0x06;
        *out++ = 0x00;
        *(u32*)out = transid; out += 4;
        *(u16*)out = 0; out += 2; // seconds elapsed
        *(u16*)out = 0; out += 2;
        *(u32*)out = htonl(0x00000000); out += 4; // client IP
        *(u32*)out = htonl(kClientIP); out += 4; // your IP
        *(u32*)out = htonl(kServerIP); out += 4; // server IP
        *(u32*)out = htonl(0x00000000); out += 4; // gateway IP
        memcpy(out, &data[6], 6); out += 6;
        memset(out, 0, 10); out += 10;
        memset(out, 0, 192); out += 192;
        *(u32*)out = 0x63538263; out += 4; // DHCP magic

        // DHCP options
        *out++ = 53; *out++ = 1;
        *out++ = (type==1) ? 2 : 5; // DHCP type: offer/ack
        *out++ = 1; *out++ = 4;
        *(u32*)out = htonl(0xFFFFFF00); out += 4; // subnet mask
        *out++ = 3; *out++ = 4;
        *(u32*)out = htonl(kServerIP); out += 4; // router
        *out++ = 51; *out++ = 4;
        *(u32*)out = htonl(442030); out += 4; // lease time
        *out++ = 54; *out++ = 4;
        *(u32*)out = htonl(kServerIP); out += 4; // DHCP server
        *out++ = 6; *out++ = 4;
        *(u32*)out = htonl(kDNSIP); out += 4; // DNS (hax)

        *out++ = 0xFF;
        memset(out, 0, 20); out += 20;

        u32 framelen = (u32)(out - &resp[0]);
        if (framelen & 1) { *out++ = 0; framelen++; }
        FinishUDPFrame(resp, framelen);

        // TODO: if there is already a packet queued, this will overwrite it
        // that being said, this will only happen during DHCP setup, so probably
        // not a big deal

        PacketLen = framelen;
        memcpy(PacketBuffer, resp, PacketLen);
        RXNum = 1;
    }
}

void HandleDNSFrame(u8* data, int len)
{
    u8* ipheader = &data[0xE];
    u8* udpheader = &data[0x22];
    u8* dnsbody = &data[0x2A];

    u32 srcip = ntohl(*(u32*)&ipheader[12]);
    u16 srcport = ntohs(*(u16*)&udpheader[0]);

    u16 id = ntohs(*(u16*)&dnsbody[0]);
    u16 flags = ntohs(*(u16*)&dnsbody[2]);
    u16 numquestions = ntohs(*(u16*)&dnsbody[4]);
    u16 numanswers = ntohs(*(u16*)&dnsbody[6]);
    u16 numauth = ntohs(*(u16*)&dnsbody[8]);
    u16 numadd = ntohs(*(u16*)&dnsbody[10]);

    printf("DNS: ID=%04X, flags=%04X, Q=%d, A=%d, auth=%d, add=%d\n",
           id, flags, numquestions, numanswers, numauth, numadd);

    // for now we only take 'simple' DNS requests
    if (flags & 0x8000) return;
    if (numquestions != 1 || numanswers != 0) return;

    u8 resp[1024];
    u8* out = &resp[0];

    // ethernet
    memcpy(out, &data[6], 6); out += 6;
    memcpy(out, kServerMAC, 6); out += 6;
    *(u16*)out = htons(0x0800); out += 2;

    // IP
    u8* resp_ipheader = out;
    *out++ = 0x45;
    *out++ = 0x00;
    *(u16*)out = 0; out += 2; // total length
    *(u16*)out = htons(IPv4ID); out += 2; IPv4ID++;
    *out++ = 0x00;
    *out++ = 0x00;
    *out++ = 0x80; // TTL
    *out++ = 0x11; // protocol (UDP)
    *(u16*)out = 0; out += 2; // checksum
    *(u32*)out = htonl(kDNSIP); out += 4; // source IP
    *(u32*)out = htonl(srcip); out += 4; // destination IP

    // UDP
    u8* resp_udpheader = out;
    *(u16*)out = htons(53); out += 2; // source port
    *(u16*)out = htons(srcport); out += 2; // destination port
    *(u16*)out = 0; out += 2; // length
    *(u16*)out = 0; out += 2; // checksum

    // DNS
    u8* resp_body = out;
    *(u16*)out = htons(id); out += 2; // ID
    *(u16*)out = htons(0x8000); out += 2; // flags
    *(u16*)out = htons(numquestions); out += 2; // num questions
    *(u16*)out = htons(numquestions); out += 2; // num answers
    *(u16*)out = 0; out += 2; // num authority
    *(u16*)out = 0; out += 2; // num additional

    u32 curoffset = 12;
    for (u16 i = 0; i < numquestions; i++)
    {
        if (curoffset >= (len-0x2A)) return;

        u8 bitlength = 0;
        while ((bitlength = dnsbody[curoffset++]) != 0)
            curoffset += bitlength;

        curoffset += 4;
    }

    u32 qlen = curoffset-12;
    if (qlen > 512) return;
    memcpy(out, &dnsbody[12], qlen); out += qlen;

    curoffset = 12;
	for (u16 i = 0; i < numquestions; i++)
	{
		// assemble the requested domain name
		u8 bitlength = 0;
		char domainname[256] = ""; int o = 0;
		while ((bitlength = dnsbody[curoffset++]) != 0)
		{
		    if ((o+bitlength) >= 255)
            {
                // welp. atleast try not to explode.
                domainname[o++] = '\0';
                break;
            }

			strncpy(&domainname[o], (const char *)&dnsbody[curoffset], bitlength);
			o += bitlength;

			curoffset += bitlength;
			if (dnsbody[curoffset] != 0)
				domainname[o++] = '.';
            else
                domainname[o++] = '\0';
		}

		u16 type = ntohs(*(u16*)&dnsbody[curoffset]);
		u16 cls = ntohs(*(u16*)&dnsbody[curoffset+2]);

		printf("- q%d: %04X %04X %s", i, type, cls, domainname);

		// get answer
		struct addrinfo dns_hint;
		struct addrinfo* dns_res;
		u32 addr_res;

		memset(&dns_hint, 0, sizeof(dns_hint));
		dns_hint.ai_family = AF_INET; // TODO: other address types (INET6, etc)
		if (getaddrinfo(domainname, "0", &dns_hint, &dns_res) == 0)
        {
            struct addrinfo* p = dns_res;
            while (p)
            {
                struct sockaddr_in* addr = (struct sockaddr_in*)p->ai_addr;
                printf(" -> %d.%d.%d.%d",
                       addr->sin_addr.S_un.S_un_b.s_b1, addr->sin_addr.S_un.S_un_b.s_b2,
                       addr->sin_addr.S_un.S_un_b.s_b3, addr->sin_addr.S_un.S_un_b.s_b4);

                addr_res = addr->sin_addr.S_un.S_addr;
                p = p->ai_next;
            }
        }
        else
        {
            printf(" shat itself :(");
            addr_res = 0;
        }

		printf("\n");
		curoffset += 4;

		// TODO: betterer support
		// (under which conditions does the C00C marker work?)
		*(u16*)out = htons(0xC00C); out += 2;
		*(u16*)out = htons(type); out += 2;
		*(u16*)out = htons(cls); out += 2;
		*(u32*)out = htonl(3600); out += 4; // TTL (hardcoded for now)
		*(u16*)out = htons(4); out += 2; // address length
		*(u32*)out = addr_res; out += 4; // address
    }

    u32 framelen = (u32)(out - &resp[0]);
    if (framelen & 1) { *out++ = 0; framelen++; }
    FinishUDPFrame(resp, framelen);

    // TODO: if there is already a packet queued, this will overwrite it
    // that being said, this will only happen during DHCP setup, so probably
    // not a big deal

    PacketLen = framelen;
    memcpy(PacketBuffer, resp, PacketLen);
    RXNum = 1;
}

void HandleIPFrame(u8* data, int len)
{
    u8 protocol = data[0x17];

    // any kind of IPv4 frame that isn't DHCP
    // we do NAT and forward it to the network

    // like:
    // melonRouter -> host
    // destination MAC set to host MAC
    // source MAC set to melonRouter MAC

    //memcpy(&data[0], &PCapAdapterData->DHCP_MAC[0], 6);
    //memcpy(&data[6], &PCapAdapterData->MAC[0], 6);

    //*(u32*)&data[0x1A] = *(u32*)&PCapAdapterData->IP_v4[0];

    u8* ipheader = &data[0xE];
    u8* protoheader = &data[0x22];

    // IP checksum
    u32 tmp = 0;

    *(u16*)&ipheader[10] = 0;
    for (int i = 0; i < 20; i += 2)
        tmp += ntohs(*(u16*)&ipheader[i]);
    while (tmp >> 16)
        tmp = (tmp & 0xFFFF) + (tmp >> 16);
    tmp ^= 0xFFFF;
    *(u16*)&ipheader[10] = htons(tmp);

    if (protocol == 0x11)
    {
        u32 udplen = ntohs(*(u16*)&protoheader[4]);

        // UDP checksum
        tmp = 0;
        *(u16*)&protoheader[6] = 0;
        tmp += ntohs(*(u16*)&ipheader[12]);
        tmp += ntohs(*(u16*)&ipheader[14]);
        tmp += ntohs(*(u16*)&ipheader[16]);
        tmp += ntohs(*(u16*)&ipheader[18]);
        tmp += ntohs(0x1100);
        tmp += udplen;
        for (u8* i = protoheader; i < &protoheader[udplen]; i += 2)
            tmp += ntohs(*(u16*)i);
        while (tmp >> 16)
            tmp = (tmp & 0xFFFF) + (tmp >> 16);
        tmp ^= 0xFFFF;
        if (tmp == 0) tmp = 0xFFFF;
        *(u16*)&protoheader[6] = htons(tmp);
    }
    else if (protocol == 0x06)
    {
        u32 tcplen = ntohs(*(u16*)&ipheader[2]) - 0x14;

        u16 srcport = ntohs(*(u16*)&protoheader[0]);
        u16 dstport = ntohs(*(u16*)&protoheader[2]);
        u16 flags = ntohs(*(u16*)&protoheader[12]);

        if (flags & 0x002) // SYN
        {
            int sockid = -1;
            for (int i = 0; i < (sizeof(TCPSocketList)/sizeof(TCPSocket)); i++)
            {
                TCPSocket* sock = &TCPSocketList[i];
                if (sock->Status == 1 && !memcmp(&sock->DestIP, &ipheader[16], 4) && sock->DestPort == dstport)
                {
                    printf("LANMAGIC: duplicate TCP socket\n");
                    sockid = i;
                    break;
                }
            }

            if (sockid == -1)
            {
                for (int i = 0; i < (sizeof(TCPSocketList)/sizeof(TCPSocket)); i++)
                {
                    TCPSocket* sock = &TCPSocketList[i];
                    if (sock->Status == 0)
                    {
                        sockid = i;
                        break;
                    }
                }
            }

            if (sockid == -1)
            {
                printf("LANMAGIC: !! TCP SOCKET LIST FULL\n");
                return;
            }

            printf("LANMAGIC: opening TCP socket #%d to %d.%d.%d.%d:%d\n",
                   sockid,
                   ipheader[16], ipheader[17], ipheader[18], ipheader[19],
                   dstport);

            // keep track of it
            // (TODO: also keep track of source port?)

            TCPSocket* sock = &TCPSocketList[sockid];
            sock->Status = 1;
            memcpy(sock->DestIP, &ipheader[16], 4);
            sock->DestPort = dstport;

            // open backend socket
            if (!sock->Backend)
            {
                sock->Backend = socket(AF_INET, SOCK_STREAM, 0);
            }

            struct sockaddr_in conn_addr;
            memset(&conn_addr, 0, sizeof(conn_addr));
            conn_addr.sin_family = AF_INET;
            conn_addr.sin_addr.S_un.S_addr = *(u32*)&ipheader[16];
            if (connect(sock->Backend, (sockaddr*)&conn_addr, sizeof(conn_addr)) == SOCKET_ERROR)
            {
                printf("connect() shat itself :(\n");
            }
        }
        else
        {
            int sockid = -1;
            for (int i = 0; i < (sizeof(TCPSocketList)/sizeof(TCPSocket)); i++)
            {
                TCPSocket* sock = &TCPSocketList[i];
                if (sock->Status == 1 && !memcmp(&sock->DestIP, &ipheader[16], 4) && sock->DestPort == dstport)
                {
                    sockid = i;
                    break;
                }
            }

            if (sockid == -1)
            {
                printf("LANMAGIC: bad TCP packet\n");
                return;
            }

            if (flags & 0x001) // FIN
            {
                // TODO: cleverer termination?
                // also timeout etc
                TCPSocketList[sockid].Status = 0;
            }
        }

        // TCP checksum
        tmp = 0;
        *(u16*)&protoheader[16] = 0;
        tmp += ntohs(*(u16*)&ipheader[12]);
        tmp += ntohs(*(u16*)&ipheader[14]);
        tmp += ntohs(*(u16*)&ipheader[16]);
        tmp += ntohs(*(u16*)&ipheader[18]);
        tmp += ntohs(0x0600);
        tmp += tcplen;
        for (u8* i = protoheader; i < &protoheader[tcplen]; i += 2)
            tmp += ntohs(*(u16*)i);
        while (tmp >> 16)
            tmp = (tmp & 0xFFFF) + (tmp >> 16);
        tmp ^= 0xFFFF;
        if (tmp == 0) tmp = 0xFFFF;
        *(u16*)&protoheader[16] = htons(tmp);
    }
}

void HandleARPFrame(u8* data, int len)
{
    u16 protocol = ntohs(*(u16*)&data[0x10]);
    if (protocol != 0x0800) return;

    u16 op = ntohs(*(u16*)&data[0x14]);
    u32 targetip = ntohl(*(u32*)&data[0x26]);

    // TODO: handle ARP to the client
    // this only handles ARP to the DHCP/router

    if (op == 1)
    {
        // opcode 1=req 2=reply
        // sender MAC
        // sender IP
        // target MAC
        // target IP

        const u8* targetmac;
        if (targetip == kServerIP)   targetmac = kServerMAC;
        else if (targetip == kDNSIP) targetmac = kDNSMAC;
        else return;

        u8 resp[64];
        u8* out = &resp[0];

        // ethernet
        memcpy(out, &data[6], 6); out += 6;
        memcpy(out, kServerMAC, 6); out += 6;
        *(u16*)out = htons(0x0806); out += 2;

        // ARP
        *(u16*)out = htons(0x0001); out += 2; // hardware type
        *(u16*)out = htons(0x0800); out += 2; // protocol
        *out++ = 6; // MAC address size
        *out++ = 4; // IP address size
        *(u16*)out = htons(0x0002); out += 2; // opcode
        memcpy(out, targetmac, 6); out += 6;
        *(u32*)out = htonl(targetip); out += 4;
        memcpy(out, &data[0x16], 6+4); out += 6+4;

        u32 framelen = (u32)(out - &resp[0]);

        // TODO: if there is already a packet queued, this will overwrite it
        // that being said, this will only happen during DHCP setup, so probably
        // not a big deal

        PacketLen = framelen;
        memcpy(PacketBuffer, resp, PacketLen);
        RXNum = 1;
    }
    else
    {
        printf("wat??\n");
    }
}

void HandlePacket(u8* data, int len)
{
    u16 ethertype = ntohs(*(u16*)&data[0xC]);

    if (ethertype == 0x0800) // IPv4
    {
        u8 protocol = data[0x17];
        if (protocol == 0x11) // UDP
        {
            u16 srcport = ntohs(*(u16*)&data[0x22]);
            u16 dstport = ntohs(*(u16*)&data[0x24]);
            if (srcport == 68 && dstport == 67) // DHCP
            {
                printf("LANMAGIC: DHCP packet\n");
                return HandleDHCPFrame(data, len);
            }
            else if (dstport == 53) // DNS
            {
                printf("LANMAGIC: DNS packet\n");
                return HandleDNSFrame(data, len);
            }
        }

        printf("LANMAGIC: IP packet\n");
        return HandleIPFrame(data, len);
    }
    else if (ethertype == 0x0806) // ARP
    {
        printf("LANMAGIC: ARP packet\n");
        return HandleARPFrame(data, len);
    }
}

int SendPacket(u8* data, int len)
{
    if (len > 2048)
    {
        printf("LAN_SendPacket: error: packet too long (%d)\n", len);
        return 0;
    }

    HandlePacket(data, len);
    return len;
}

int RecvPacket(u8* data)
{
    int ret = 0;
    if (RXNum > 0)
    {
        memcpy(data, PacketBuffer, PacketLen);
        ret = PacketLen;
        RXNum = 0;
    }

    // TODO: check sockets
    return ret;
}

}