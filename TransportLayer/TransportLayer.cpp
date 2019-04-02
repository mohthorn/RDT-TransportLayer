/* Main, call Socket operations
/*
 * CPSC 612 Spring 2019
 * HW3
 * by Chengyi Min
 */

#include "pch.h"
#include "SenderSocket.h"

int main(int argc, char **argv)
{
	if (argc != 8)
	{
		printf("Usage: executable destination_server buffer_size sender_window round_trip_delay loss_rate bottleneck_speed\n");
		exit(0);
	}

	char * targetHost = argv[1];
	int power = atoi(argv[2]);
	int senderWindow = atoi(argv[3]);
	int status = NOT_CONNECTED;
	LinkProperties lp;
	lp.RTT = atof(argv[4]);
	lp.speed = 1e6 * atof(argv[7]); // convert to megabits
	lp.pLoss[FORWARD_PATH] = atof(argv[5]);
	lp.pLoss[RETURN_PATH] = atof(argv[6]);
	UINT64 dwordBufSize = (UINT64)1 << power;
	DWORD *dwordBuf = new DWORD[dwordBufSize]; // user-requested buffer

	printf("Main:   sender W = %d, RTT %.3lf sec, loss %g / %g, link %g Mbps\n",
		senderWindow,
		lp.RTT,
		lp.pLoss[FORWARD_PATH],
		lp.pLoss[RETURN_PATH],
		lp.speed*1.0/1e6
			);
	printf("Main:   initializing DWORD array with 2 ^ %d elements... ", power);
	clock_t start;
	clock_t end;
	clock_t duration;
	start = clock();

	for (UINT64 i = 0; i < dwordBufSize; i++) // required initialization
		dwordBuf[i] = i;
	end = clock();
	duration = 1000.0*(end - start) / (double)(CLOCKS_PER_SEC);
	printf("done in %d ms\n", duration);
	
	WSADATA wsaData;

	//Initialize WinSock; once per program run
	WORD wVersionRequested = MAKEWORD(2, 2);
	if (WSAStartup(wVersionRequested, &wsaData) != 0) {
		printf("WSAStartup error %d\n", WSAGetLastError());
		delete dwordBuf;
		WSACleanup();
		exit(0);
	}


	SenderSocket ss; // instance of your class
	start = clock();
	if ((status = ss.Open(targetHost, MAGIC_PORT, senderWindow, &lp)) != STATUS_OK)
	{
		printf("Main:   connect failed with status %d\n", status);
		delete dwordBuf;
		ss.opened = 0;
		return 0;
		// error handling: print status and quit
	}



	if(status == STATUS_OK)
		printf("Main:   connected to %s in %.3lf sec, pkt size %d bytes\n", targetHost, ss.sampleRTT*1.0/1e3, MAX_PKT_SIZE);
	else
	{
		printf("Main:   connect failed with status %d\n", status);
		delete dwordBuf;
		ss.opened = 0;
		return 0;
	}
	start = clock();
	char *charBuf = (char*)dwordBuf; // this buffer goes into socket
	UINT64 byteBufferSize = dwordBufSize << 2; // convert to bytes
	UINT64 off = 0; // current position in buffer
	while (off < byteBufferSize)
	{
		// decide the size of next chunk
		int bytes = min(byteBufferSize - off, MAX_PKT_SIZE - sizeof(SenderDataHeader));
		// send chunk into socket if ((status = ss.Send (charBuf + off, bytes)) != STATUS_OK)
		if ((status = ss.Send(charBuf + off, bytes)) != STATUS_OK)
		{
			printf("Main:   send error with status %d\n", status);
			return FAILCODE;
		}
		// error handing: print status and quit
		off += bytes;
	}
	end = clock();
	duration = 1000000.0* (end - start) / (double)(CLOCKS_PER_SEC);

	if ((status = ss.Close()) != STATUS_OK)
	{
		printf("Main:   connect failed with status %d\n", status);
		delete dwordBuf;
		return 0;
		// error handling: print status and quit
	}

	printf("Main:   transfer finished in %.3lf sec\n", duration/1e6);
	delete dwordBuf;
		// error handing: print status and quit

}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
