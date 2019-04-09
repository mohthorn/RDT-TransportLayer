/*
 * CPSC 612 Spring 2019
 * HW3
 * by Chengyi Min
 */
#pragma once
class Checksum
{
public:
	DWORD crc_table[256];
	Checksum();
	DWORD CRC32(unsigned char *buf, size_t len);
	~Checksum();
};

