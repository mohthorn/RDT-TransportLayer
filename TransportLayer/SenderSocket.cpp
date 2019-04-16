/* SenderSocket Class, Open(), Close() and Send()
/*
 * CPSC 612 Spring 2019
 * HW3
 * by Chengyi Min
 */
#include "pch.h"
#include "SenderSocket.h"

#define MAX_SYN_ATMP 3
#define MAX_FIN_ATMP 5
#define BETA 0.25
#define ALPHA 0.125
#define MAX_ATTEMPT 50

DWORD StatusThread(LPVOID pParam)
{
	Parameters *p = (Parameters*)pParam;
	clock_t start;
	clock_t end;
	clock_t duration;
	
	start = clock();
	clock_t prevTime;
	double prevV = 0;
	end = clock();
	prevTime = 1000.0* (end - start) / (double)(CLOCKS_PER_SEC);
	while (true)
	{	
		
		Sleep(2000);
		end = clock();
		duration = 1000.0* (end - start) / (double)(CLOCKS_PER_SEC);
		//printf("[%2d] B %6d (  %.1lf MB) N %6d T %d F %d W %d S %.3lf Mbps  RTT %.3lf\n",
		//	(int)(duration/1000.0),
		//	p->B,
		//	floor(p->V / (1024.0*1024.0)*10.0+0.5)/10.0,
		//	p->N,
		//	p->T,
		//	p->F,
		//	p->W,
		//	(p->V - prevV)*(8.0/1000.0) / (duration - prevTime ),
		//	p->RTT);
		prevV = p->V;
		prevTime = duration;
	}
	return 0;
}

DWORD WINAPI ThreadStarter(LPVOID p)
{

	SenderSocket *ss = (SenderSocket *)p;

	Parameters pParam;
	pParam.T = 0;
	pParam.F = 0;
	pParam.V = 0;
	pParam.S = 0;
	pParam.RTT = ss->RTO;
	HANDLE status_handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)StatusThread, (LPVOID)&pParam, 0, NULL);
	ss->WorkThread((LPVOID)&pParam);
	return 0;
}

SenderSocket::SenderSocket()
{

}

SenderSocket::SenderSocket(UINT64 W)
{
	opened = 0;
	creationTime = clock();
	RTO = 1;
	sampleRTT = 0;
	nextSeq = 0;
	this->W = W;
	effectiveWin = 1;
	pending_pkts = new Packet[W];
	bufferFin = FALSE;
	empty = CreateSemaphore(NULL, 0, W, NULL);
	lastACK = 0;
	timeArr = new DWORD[W];
	if (empty == NULL)
	{
		printf("CreateSemaphore error: %d\n", GetLastError());
		exit;
	}
	full = CreateSemaphore(NULL, 0, W, NULL);
	if (full == NULL)
	{
		printf("CreateSemaphore error: %d\n", GetLastError());
		exit;
	}
	eventQuit = CreateEvent(NULL, true, false, NULL); // auto reset
	if (eventQuit == NULL)
	{
		printf("CreateSemaphore error: %d\n", GetLastError());
		exit;
	}
	socketReceiveReady = CreateEvent(NULL, true, false, NULL); // auto reset
	if (socketReceiveReady == NULL)
	{
		printf("CreateSemaphore error: %d\n", GetLastError());
		exit;
	}
}


SenderSocket::~SenderSocket()
{

}

int SenderSocket::Open(char * targetHost, int receivePort, int senderWindow, LinkProperties * linkProp)
{
	clock_t end;
	clock_t duration;
	clock_t prev;

	if (opened == 1)
	{
		end = clock();
		duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
		printf("[%.3lf] -- > ", duration*1.0 / 1e6);
		printf("second call to ss.Open() without closing connection\n", targetHost);
		closesocket(sock);
		return ALREADY_CONNECTED;
	}

	opened = 1;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET)
	{
		printf("socket generate error %d\n", WSAGetLastError());
		closesocket(sock);
		return FAILCODE;
	}
	
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	local.sin_port = htons(0);
	if (bind(sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR)
	{
		printf("socket error %d", WSAGetLastError());
		closesocket(sock);
		return FAILCODE;
	}

	memset(&remote, 0, sizeof(remote));
	remote.sin_family = AF_INET;
	remote.sin_port = htons(receivePort);

	DWORD IP = inet_addr(targetHost);
	struct hostent *remoteHostName;
	if (IP == INADDR_NONE)
	{
		// if not a valid IP, then do a DNS lookup
		if ((remoteHostName = gethostbyname(targetHost)) == NULL)
		{
			end = clock();
			duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
			printf("[%.3lf] -- > ", duration*1.0 / 1e6);
			printf("target %s is invalid\n", targetHost);
			closesocket(sock);
			return INVALID_NAME;
		}
		else // take the first IP address and copy into sin_addr
			memcpy((char *)&(remote.sin_addr), remoteHostName->h_addr, remoteHostName->h_length);
	}
	else
		remote.sin_addr.S_un.S_addr = IP;

	SenderSynHeader ssh;
	ssh.sdh.flags.SYN = 1;
	ssh.sdh.seq = 0;
	ssh.lp = *linkProp;
	ssh.lp.bufferSize = senderWindow + MAX_FIN_ATMP;

	lp = ssh.lp;

	for (int i = 0; i < MAX_SYN_ATMP; i++)
	{
		end = clock();
		duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);

#if LOGGING
		printf("[%.3lf] -- > ", duration*1.0 / 1e6);
		printf("SYN %d (attempt %d of %d, RTO %.3lf) to %s\n",
			ssh.sdh.seq,
			i + 1,
			MAX_SYN_ATMP,
			RTO,
			inet_ntoa(remote.sin_addr)
		);
#endif
		if (sendOnePacket((char*)(&ssh), sizeof(SenderSynHeader)) == SOCKET_ERROR)
			return FAILED_SEND;

		ReceiverHeader rh;

		fd_set fd;
		FD_ZERO(&fd); // clear the set 
		FD_SET(sock, &fd); // add your socket to the set
		TIMEVAL *timeout = new TIMEVAL;

		timeout->tv_sec = floor(RTO);
		timeout->tv_usec = 1e6 * (RTO-floor(RTO));

		int recvSize = 0;
		int available = select(0, &fd, NULL, NULL, timeout);
		if (available > 0)
		{
			if (recvOnePacket((char *)&rh, sizeof(ReceiverHeader)) == FAILED_RECV)
				return FAILED_RECV;
			
			prev = end;
			end = clock();
			sampleRTT = end - prev;
			duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
			

			//update RTO on successful tx
			RTO = 3.0* (sampleRTT) / (double)(CLOCKS_PER_SEC);
			estRTT = (sampleRTT) / (double)(CLOCKS_PER_SEC);
#if LOGGING
			printf("[%.3lf] < -- ", duration*1.0 / 1e6);
			printf("SYN-ACK %d window %d; setting initial RTO to %.3lf\n", 
				rh.ackSeq, 
				rh.recvWnd,
				RTO);
#endif
			lastReleased = min(W, rh.recvWnd);
			ReleaseSemaphore(empty, lastReleased, NULL);
			//start working thread
			work_handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadStarter, this, 0, NULL);

			return STATUS_OK;
		}
		else
		{
			if (available == 0)
				continue;
			else
			{
				end = clock();
				duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
				printf("[%.3lf] -- > ", duration*1.0 / 1e6);
				printf("failed recvfrom with %d\n", WSAGetLastError());
				closesocket(sock);
				return FAILED_RECV;
			}
		}
	}
	return TIMEOUT;
}

int SenderSocket::Close(double &elapsedTime)
{
	clock_t end;
	clock_t duration;
	clock_t prev;

	SetEvent(eventQuit);
	WaitForSingleObject(work_handle, INFINITE);
	CloseHandle(work_handle);
	data_end = clock();
	elapsedTime = 1000.0* (data_end - data_start) / (double)(CLOCKS_PER_SEC);
	if (!opened)
	{
		end = clock();
		duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
		printf("[%.3lf] -- > ", duration*1.0 / 1e6);
		printf("call to ss.Send()/Close() without ss.Open()\n");
		closesocket(sock);
		return NOT_CONNECTED;
	}

	SenderSynHeader ssh;
	ssh.sdh.flags.FIN = 1;
	ssh.sdh.seq = nextSeq;
	ssh.lp = lp;

	for (int i = 0; i < MAX_FIN_ATMP; i++)
	{
		if (sendOnePacket((char*)(&ssh), sizeof(SenderSynHeader)) == SOCKET_ERROR)
			return FAILED_SEND;

		end = clock();
		duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
#if LOGGING
		printf("[%.3lf] -- > FIN %d (attempt %d of %d, RTO %.3lf)\n",
				duration *1.0/1e6,
				ssh.sdh.seq,
				i+1,
				MAX_FIN_ATMP,
				RTO
				);
#endif
		ReceiverHeader rh;

		fd_set fd;
		FD_ZERO(&fd); // clear the set 
		FD_SET(sock, &fd); // add your socket to the set
		TIMEVAL *timeout = new TIMEVAL;

		timeout->tv_sec = floor(RTO);
		timeout->tv_usec = 1e6 * (RTO - floor(RTO));

		int recvSize = 0;
		int available = select(0, &fd, NULL, NULL, timeout);
		if (available > 0)
		{
			if (recvOnePacket((char *)&rh, sizeof(ReceiverHeader)) == FAILED_RECV)
				return FAILED_RECV;

			end = clock();
			duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
			printf("[%.2lf] < -- FIN-ACK %d window %X\n",
				duration *1.0 / 1e6,
				rh.ackSeq,
				rh.recvWnd
			);
			opened = 0;
			return STATUS_OK;
		}
		else
		{
			if (available == 0)
				continue;
			else
			{
				end = clock();
				duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
				printf("[%.3lf] < -- ", duration*1.0 / 1e6);
				printf("failed recvfrom with %d\n", WSAGetLastError());
				closesocket(sock);
				return FAILED_RECV;
			}
		}
	}
	return TIMEOUT;
}

int SenderSocket::Send(char * buf, int bytes)
{
	clock_t end;
	clock_t duration;
	clock_t prev;

	if (!opened)
	{
		end = clock();
		duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
		printf("[%.3lf] -- > ", duration*1.0 / 1e6);
		printf("call to ss.Send()/Close() without ss.Open()\n");
		closesocket(sock);
		return NOT_CONNECTED;
	}

	HANDLE arr[] = {empty,eventQuit };
	int ret = WaitForMultipleObjects(2, arr, false, INFINITE);

	if (ret == WAIT_OBJECT_0)
	{
		// no need for mutex as no shared variables are modified
		UINT64 slot = nextSeq % W;
		Packet *p = pending_pkts + slot; // pointer to packet struct
		p->buf = new char[MAX_PKT_SIZE];
		SenderDataHeader *sdh = (SenderDataHeader*)(p->buf);
		sdh->seq = nextSeq;
		sdh->flags.reserved = 0;
		sdh->flags.SYN = 0;
		sdh->flags.ACK = 0;
		sdh->flags.FIN = 0;
		sdh->flags.magic = MAGIC_PROTOCOL;
		p->seq = nextSeq;
		p->size = bytes + sizeof(SenderDataHeader);
		memcpy(sdh + 1, buf, bytes);
		int sequence = *(int*)(p->buf + sizeof(SenderDataHeader));

		nextSeq++;
		ReleaseSemaphore(full, 1, NULL);
		return STATUS_OK;
	}
	return STATUS_OK;
}

int SenderSocket::WorkThread(LPVOID pParam)
{
	Parameters *p = (Parameters*)pParam;

	clock_t start;
	clock_t end;
	clock_t duration;
	clock_t prev;

	HANDLE events[] = { full,socketReceiveReady };
	nextToSend = 0;

	fd_set fd;
	FD_ZERO(&fd); // clear the set 
	FD_SET(sock, &fd); // add your socket to the set
	TIMEVAL *timeout = new TIMEVAL;

	timeout->tv_sec = floor(RTO);
	timeout->tv_usec = 1e6 * (RTO - floor(RTO));
	clock_t timerStart = clock();
	
	int retransFlag = 0;
	int lastAttempt = -1;
	int attemptCount = 0;
	int recvAttempt = 1;

	while (true)
	{	
		DWORD WaitTimeout;
		DWORD timerExpire = 1.0*estRTT *1000;

		if (bufferFin == TRUE) // all packets on buf
		{
			/*printf("lastACK %d, nextSeq %d\n", lastACK, nextSeq);*/
			if (lastACK == nextSeq)	//all sent and acked
			{
				WaitForSingleObject(eventQuit, INFINITE);
				return 0;
			}
		}
		end = clock();
		duration = 1000* (end - timerStart) / (double)(CLOCKS_PER_SEC);
		WaitTimeout = timerExpire - duration;
		if (timerExpire < duration)
		{
			WaitTimeout = 1;

		}
		int ret = WaitForMultipleObjects(2, events, false, WaitTimeout);

		switch (ret)
		{
			case WAIT_TIMEOUT:		//retransmit
			{
				if (lastAttempt == sndBase)
					attemptCount++;
				else
				{
					lastAttempt = sndBase;
					attemptCount = 1;
				}
				if (attemptCount == MAX_ATTEMPT)
				{
					printf("Exceeding Max Attempt\n");
					exit(0);
				}
				
				//retransmission begin 
				Packet * sndP = &pending_pkts[sndBase%W];
				printf("retransmitt %d, nextToSend %d\n", sndP->seq, nextToSend);
				if (sendOnePacket((char*)(sndP->buf), sndP->size) == SOCKET_ERROR)
					return FAILED_SEND;
				retransFlag = 1;
				timerStart = clock();
				start = clock();
				SetEvent(socketReceiveReady);
				break;				
			}
			case WAIT_OBJECT_0:	//send a packet
			{
				Packet * sndP = &pending_pkts[nextToSend%W];
				printf("Sent %d,nextToSend %d, W %d \n", sndP->seq, nextToSend, W);
				if (sendOnePacket((char*)(sndP->buf), sndP->size) == SOCKET_ERROR)
					return FAILED_SEND;
				if (nextToSend%W == 0)
				{
					start = clock();
					timerStart = clock();
					
				}
				timeArr[nextToSend%W] = clock();
				
				nextToSend++;	
				SetEvent(socketReceiveReady);
				break;
			}
			case WAIT_OBJECT_0+1: //receive
			{
				timeout->tv_sec = floor(RTO);
				timeout->tv_usec = 1e6 * (RTO - floor(RTO));
				
				ReceiverHeader rh;
				fd_set fd;
				FD_ZERO(&fd); // clear the set 
				FD_SET(sock, &fd); // add your socket to the set
				int available = select(0, &fd, NULL, NULL, timeout);
				if (available > 0)
				{
					if (recvOnePacket((char *)&rh, sizeof(ReceiverHeader)) == FAILED_RECV)
						return FAILED_RECV;
					lastACK = rh.ackSeq;
					end = clock();
					if (lastACK > sndBase)
					{
						if (recvAttempt == 1)
						{
							sampleRTT = (end - timeArr[(lastACK - 1) % W]) / (double)(CLOCKS_PER_SEC);
							devRTT = (1 - BETA)*devRTT + BETA * fabs(sampleRTT - estRTT);
							estRTT = (1 - ALPHA)*estRTT + ALPHA * sampleRTT;
							RTO = estRTT + 4 * max(devRTT, 0.010);
							p->RTT = estRTT;
						}
						else
						{
							if (recvAttempt % 3 == 0)
							{

							}
						}
						sndBase = lastACK;
						effectiveWin = min(W, rh.recvWnd);
						int newReleased = sndBase + effectiveWin - lastReleased;
						printf("effectWin %d, sndBase %d release %d last release %d\n",effectiveWin, sndBase, newReleased, lastReleased);
						ReleaseSemaphore(empty, newReleased, NULL);
						lastReleased += newReleased;
						recvAttempt = 1;
					}
					recvAttempt++;

					printf("Received ACK %d, recvWind %X\n", rh.ackSeq, rh.recvWnd);

					p->W = min(W, rh.recvWnd);

					//recalculate RTO

					//printf("RTO: %d.%d\n", timeout->tv_sec, timeout->tv_usec);

					//printf("new RTO %lf\n", RTO);
					
					p->N = lastACK;
					
					
					p->B = nextToSend - W;
				}
				else
				{
					if (available == 0)
					{
						//printf("RTO: %d.%d\n", timeout->tv_sec, timeout->tv_usec);
						duration = 1000000.0*(end - timerStart) / (double)(CLOCKS_PER_SEC);
						ResetEvent(socketReceiveReady);
						//printf("[%.3lf] -- > ", duration*1.0 / 1e6);
						//printf("reveived Nothing\n");
						continue;
					}
					else
					{
						//printf("[%.3lf] -- > ", duration*1.0 / 1e6);
						printf("failed recvfrom with %d\n", WSAGetLastError());
						closesocket(sock);
						return FAILED_RECV;
					}
				}
				break;
			}
		}	
	}
	return 0;
}



int SenderSocket::sendOnePacket(char * pack, int size)
{
	clock_t end;
	clock_t duration;
	if (sendto(sock, (char*)pack, size, 0, (struct sockaddr*)&remote, sizeof(sockaddr)) == SOCKET_ERROR)
	{
		end = clock();
		duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
		printf("[%.3lf] -- > ", duration*1.0 / 1e6);
		printf("failed sendto with %d\n", WSAGetLastError());
		closesocket(sock);
		return SOCKET_ERROR;
	}
	return STATUS_OK;
}

int SenderSocket::recvOnePacket(char * pack, int size)
{
	int recvSize = 0;
	clock_t end;
	clock_t duration;
	struct sockaddr_in response;
	int responseSize = sizeof(response);
	if ((recvSize = recvfrom(sock, pack, size, 0, (struct sockaddr*) &response, &responseSize)) < 0)
	{
		end = clock();
		duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
		printf("[%.3lf] < -- ", duration*1.0 / 1e6);
		printf("failed recvfrom with %d\n", WSAGetLastError());
		closesocket(sock);
		return FAILED_RECV;
	}
	return STATUS_OK;
}


