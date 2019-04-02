/* SenderSocket Class, Open(), Close() and Send(), document RTO and flags for connections
/*
 * CPSC 612 Spring 2019
 * HW3
 * by Chengyi Min
 */
#pragma once
#define MAGIC_PORT 22345 // receiver listens on this port
#define MAX_PKT_SIZE (1500-28) // maximum UDP packet size accepted by receiver
#include "Headers.h"

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

	SenderSocket();
	~SenderSocket();
	int Open(char * targetHost, int receivePort, int senderWindow, LinkProperties * linkProp);
	int Close();
	int Send(char * buf, int bytes);
};

