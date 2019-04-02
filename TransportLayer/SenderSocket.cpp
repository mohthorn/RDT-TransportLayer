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

SenderSocket::SenderSocket()
{
	opened = 0;
	creationTime = clock();
	RTO = 1;
	sampleRTT = 0;
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
		printf("[%.3lf] -- > ", duration*1.0 / 1e6);
		printf("SYN %d (attempt %d of %d, RTO %.3lf) to %s\n",
			ssh.sdh.seq,
			i + 1,
			MAX_SYN_ATMP,
			RTO,
			inet_ntoa(remote.sin_addr)
		);
		if (sendto(sock, (char*)&ssh, sizeof(SenderSynHeader), 0, (struct sockaddr*)&remote, sizeof(remote)) == SOCKET_ERROR)
		{
			end = clock();
			duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
			printf("[%.3lf] -- > ", duration*1.0 / 1e6);
			printf("failed sendto with %d\n", WSAGetLastError());
			closesocket(sock);
			return FAILED_SEND;
		}


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
			struct sockaddr_in response;
			int responseSize = sizeof(response);
			if ((recvSize = recvfrom(sock, (char*)&rh, sizeof(ReceiverHeader), 0, (struct sockaddr*) &response, &responseSize)) < 0)
			{
				end = clock();
				duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
				printf("[%.3lf] < -- ", duration*1.0 / 1e6);
				printf("failed recvfrom with %d\n", WSAGetLastError());
				closesocket(sock);
				return FAILED_RECV;
			}
			
			prev = end;
			end = clock();
			sampleRTT = end - prev;
			duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
			
			//update RTO on successful tx
			RTO = 3.0* (sampleRTT) / (double)(CLOCKS_PER_SEC);

			printf("[%.3lf] < -- ", duration*1.0 / 1e6);
			printf("SYN-ACK %d window %d; setting initial RTO to %.3lf\n", 
				rh.ackSeq, 
				rh.recvWnd,
				RTO);
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

int SenderSocket::Close()
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

	SenderSynHeader ssh;
	ssh.sdh.flags.FIN = 1;
	ssh.sdh.seq = 0;
	ssh.lp = lp;

	for (int i = 0; i < MAX_FIN_ATMP; i++)
	{
		if (sendto(sock, (char*)&ssh, sizeof(SenderSynHeader), 0, (struct sockaddr*)&remote, sizeof(remote)) == SOCKET_ERROR)
		{
			end = clock();
			duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
			printf("[%.3lf] -- > ", duration*1.0 / 1e6);
			printf("failed sendto with %d\n", WSAGetLastError());
			closesocket(sock);
			return FAILED_SEND;
		}


		end = clock();
		duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
		printf("[%.3lf] -- > FIN %d (attempt %d of %d, RTO %.3lf)\n",
				duration *1.0/1e6,
				ssh.sdh.seq,
				i+1,
				MAX_FIN_ATMP,
				RTO
				);

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
			struct sockaddr_in response;
			int responseSize = sizeof(response);
			if ((recvSize = recvfrom(sock, (char*)&rh, sizeof(ReceiverHeader), 0, (struct sockaddr*) &response, &responseSize)) < 0)
			{
				end = clock();
				duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
				printf("[%.3lf] < -- ", duration*1.0 / 1e6);
				printf("failed recvfrom with %d\n", WSAGetLastError());
				closesocket(sock);
				return FAILED_RECV;
			}

			end = clock();
			duration = 1000000.0* (end - creationTime) / (double)(CLOCKS_PER_SEC);
			printf("[%.3lf] < -- FIN-ACK %d window %d\n",
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
	return STATUS_OK;
}
