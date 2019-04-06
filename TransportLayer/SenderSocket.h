/* SenderSocket Class, Open(), Close() and Send(), document RTO and flags for connections
/*
 * CPSC 612 Spring 2019
 * HW3
 * by Chengyi Min
 */
#pragma once
#define MAGIC_PORT 22345 // receiver listens on this port
#define MAX_PKT_SIZE (1500-28) // maximum UDP packet size accepted by receiver
#define TSYN 0
#define TFIN 1
#define TDATA 2
#include "Headers.h"


using namespace std;

class Parameters
{
public:
	HANDLE mutex;
	HANDLE eventQuit;
	UINT64 B;
	UINT64 N;
	UINT64 T;
	UINT64 F;
	UINT64 W;
	double S;
	double RTT;
};

class SenderSocket
{
public:
	SOCKET sock;
	struct sockaddr_in local;
	struct sockaddr_in remote;
	LinkProperties lp;
	clock_t creationTime;
	double RTO;
	double sampleRTT;
	int opened;
	UINT64 nextSeq;
	UINT64 W;
	Packet *pending_pkts;

	SenderSocket();
	~SenderSocket();
	int Open(char * targetHost, int receivePort, int senderWindow, LinkProperties * linkProp);
	int Close();
	int Send(char * buf, int bytes);
	int WorkThread(LPVOID pParam);
	int sendOnePacket(char * pack, int size);
	int recvOnePacket(char * pack, int size);
};

