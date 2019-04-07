#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <ctype.h>
#include <stdint.h>
#include "flux.h"

#define MAX_SECTOR 31
#define RESYNCTIME	100
#define SYNCCNT	32

struct {
	byte sector;
	byte track;
	byte data[128];
	byte bsector, btrack, fsector, ftrack;
	byte crc1, crc2;
} sectors[MAX_SECTOR + 1];

bool matched[MAX_SECTOR + 1];


enum {
	INDEX_HOLE_MARK,
	INDEX_ADDRESS_MARK,
	ID_ADDRESS_MARK,
	DATA_ADDRESS_MARK,
	DELETED_ADDRESS_MARK
};

#define JITTER_ALLOWANCE        20

#define BYTES_PER_LINE  16      // for display of data dumps

typedef struct {
	char* name;              // format name
	int(*getBit)();        // get next bit routine
	int(*getMarker)();     // get marker routine
	byte    mode;
	word    indexAM;            // index address marker -- markers are stored with marker sig in high byte, marker in low
	word    idAM;               // id address marker
	word    dataAM;             // data address marker
	word    deletedAM;          // deleted data address marker
	short   fixedSector;        // min size of sector excluding data
	short   gap4;               // min gap4 size
	byte    imdMode;            // mode byte for IMD image
	byte    profile;            // disk profile
	byte    clockX2;            // 2 x clock in samples for 500kbs this is USCLOCK
	double  byteClock;          // 1 byte in samples
	unsigned short crcInit;
} format_t;

enum {
	BPS500 = 0, BPS300, BPS250
};

enum {  // mode
	NORMAL, HP
};
enum {      // profiles
	UNKNOWN,
	FM250, FM300, FM500,
	MFM250, MFM300, MFM500,
	M2FM500

};
format_t* curFmt, formats[] = {
	{"FM 500 kbps",      getFMBit, NULL, NORMAL, 0x28fc, 0x38fe, 0x38fb, 0x38f8, 60, 170, 0,   FM500,     USCLOCK, 32 * USCLOCK_D, 0xffff},
};

struct {
	long slotByteNumber;
	byte slot;
	int indexMarkerByteCnt;
	int interMarkerByteCnt;
	int interSectorByteCnt;
} slotInfo;

imd_t curTrack;

int time2Byte(long time) {
	return (int)(time / curFmt->byteClock + 0.5);
}

int getSlot(int marker) {
	int t = time2Byte(when(0));
	int slotInc;

	//   printf("%d - %d -", marker, t);
	switch (marker) {
	case INDEX_HOLE_MARK:
		slotInfo.slotByteNumber = 0;           // reset for new track copy
		slotInfo.slot = 0;
		break;
	case DELETED_ADDRESS_MARK:
	case DATA_ADDRESS_MARK:
		if (t - slotInfo.slotByteNumber < 128)   // less than a sector away
			break;
		t -= slotInfo.interMarkerByteCnt;
	case ID_ADDRESS_MARK:
		if (slotInfo.slotByteNumber == 0) {
			slotInfo.slotByteNumber = t;              // back up to slot 0 if first sector missing
			while (slotInfo.slotByteNumber > slotInfo.interSectorByteCnt)
				slotInfo.slotByteNumber -= slotInfo.interSectorByteCnt;
			slotInfo.slot = 0;
		}
		for (slotInc = 0; t - slotInfo.slotByteNumber > slotInfo.interSectorByteCnt - JITTER_ALLOWANCE; slotInc++)
			slotInfo.slotByteNumber += slotInfo.interSectorByteCnt;
		if (slotInc > 3)
			logger(ALWAYS, "Warning %d consecutive sectors missing\n", slotInc);
		slotInfo.slot += slotInc;
		if (slotInc != 0)
			slotInfo.slotByteNumber = t;          // reset base point
	}
	//    printf(" %d\n", slotInfo.slot);
	return slotInfo.slot;
}


byte invert(byte in) {      // flip bits around to support hp crc check
	byte out = 0;
	for (int i = 0; i < 8; i++, in >>= 1)
		out = (out << 1) + (in & 1);
	return out;
}


bool crcCheck(const uint8_t * data, uint16_t size) {
#define CRC16 0x8005
#define INIT 0

	uint16_t crc = INIT;
	int bits_read = 0, bit_flag;

	/* Sanity check: */
	if (data == NULL)
		return 0;

	while (size > 2)
	{
		bit_flag = crc >> 15;

		/* Get next bit: */
		crc <<= 1;
		crc |= (*data >> (7 - bits_read)) & 1; // item a) work from the most significant bits

		/* Increment bit counter: */
		bits_read++;
		if (bits_read > 7)
		{
			bits_read = 0;
			data++;
			size--;
		}

		/* Cycle check: */
		if (bit_flag)
			crc ^= CRC16;

	}

	// item b) "push out" the last 16 bits
	int i;
	for (i = 0; i < 16; ++i) {
		bit_flag = crc >> 15;
		crc <<= 1;
		if (bit_flag)
			crc ^= CRC16;
	}

	return crc == (data[0] * 256 + data[1]);      // check with recorded crc
}


// read data into buffer and check crc  ok
// returns bytes read + marker if CRC error or bad flux transition
int getData(int marker, byte * buf) {
	int toRead;
	int byteCnt = 0;
	int val;

	switch (marker) {
	case ID_ADDRESS_MARK:
		marker = curFmt->idAM & 0xff;      // replace with real marker to make crc work
		toRead = curFmt->mode == HP ? 5 : 7;
		break;
	case DATA_ADDRESS_MARK:
		marker = curFmt->dataAM & 0xff;
		toRead = 3 + curTrack.size;
		break;
	case DELETED_ADDRESS_MARK:
		marker = curFmt->deletedAM & 0xff;
		toRead = 3 + curTrack.size;
		break;
	default:
		return 0;
	}

	for (buf[byteCnt++] = marker; byteCnt < toRead; buf[byteCnt++] = val) {
		val = 0;
		for (int bitCnt = 0; bitCnt < 8; bitCnt++) {
			if (curFmt->mode == HP)
				val >>= 1;
			else
				val <<= 1;
			switch (curFmt->getBit(false)) {
			case BIT0: break;
			case BIT1: val += (curFmt->mode == HP) ? 0x80 : 1; break;
			default:
				bitLog(BITFLUSH);
				return -byteCnt;
			}
		}
	}
	bitLog(BITFLUSH);
	if (curFmt->mode == HP) {
		buf++;
		byteCnt--;
	}
	return crcCheck(buf, byteCnt) ? byteCnt : -byteCnt;    // flag if crc error
}

void dumpBuf(byte * buf, int length) {
	char text[BYTES_PER_LINE + 1];

	if (debug < VERYVERBOSE)
		return;

	for (int i = 0; i < length; i += BYTES_PER_LINE) {
		printf("%04x:", i);
		for (int j = 0; j < BYTES_PER_LINE; j++)
			if (i + j < length) {
				printf(" %02X", buf[i + j]);
				if (' ' <= buf[i + j] && buf[i + j] <= '~')
					text[j] = buf[i + j];
				else
					text[j] = '.';
			}
			else {
				printf(" --");
				text[j] = 0;
			}
		text[BYTES_PER_LINE] = 0;
		printf(" |%s|\n", text);
	}
}



int getFMByte(bool resync)
{
	int bit;
	int bitcnt = 0;
	int byte = 0;
	int syncCnt;
	if (resync) {
		while ((bit = getFMBit()) != BIT0)
			if (bit == BITEND)
				return -1;
		syncCnt = 0;
		while ((bit = getFMBit()) != BIT1)
			if (bit == BITEND)
				return -1;
			else
				syncCnt++;
		logger(VERYVERBOSE, "synced on %d bits\n", syncCnt);
		byte++;
		bitcnt++;
	}
	do {
		bit = getFMBit();
		if (bit == BITBAD) {
			logger(VERBOSE, "bad bit\n");
		}
		if (bit == BITEND)
			return -1;
		byte = (byte << 1) + (bit == BIT1 || bit == BIT1M);
	} while (++bitcnt != 8);
	return byte;

}

void flux2track()
{
	byte data[2 + 128 + 4 + 2];
	int dbyte;
	int rcnt;
	int secId;
	bool done;

	if (!analyseFormat())
		return;
	for (int i = 0; i <= MAX_SECTOR; i++) {
		sectors[i].sector = 255;
		matched[i] = false;
	}
	for (int blk = 0; seekBlock(blk); blk++) {
		done = false;
		for (int initSync = 16; !done && initSync <= 96; initSync += 16) {
			for (int resync = 2048; !done && resync > 16; resync /= 2) {
				seekBlock(blk);			// retry with different sync period
				resetGetFMBit(resync, initSync);
				memset(data, 0, sizeof(data));
				if ((dbyte = getFMByte(true)) < 0)
					continue;
				data[0] = dbyte;
				for (rcnt = 1; rcnt < sizeof(data); rcnt++)
					if ((dbyte = getFMByte(false)) < 0)
						break;
					else
						data[rcnt] = dbyte;
				if (rcnt == sizeof(data) && crcCheck(data, sizeof(data))) {
					secId = data[0] & 0x7f;
					if (secId > MAX_SECTOR || data[1] > 76) {
						printf("bad track/sector %d/%d\n", data[1], secId);
						continue;
					}
					if (sectors[secId].sector == 255)
						memcpy(&sectors[secId].sector, data, sizeof(sectors[0]));
					else if (memcmp(&sectors[data[0] & 0x7f], data, sizeof(sectors[0])) == 0)
						matched[secId] = true;
					else
						printf("different info for track/sector %d/%d\n", data[1], secId);
//					printf("%d/%d init %d resync %d\n", secId, data[1], initSync, resync);
					done = true;
				} else
					logger(VERBOSE, "failed blk %d init sync %d resync %d\n", blk, initSync, resync);
			}
		}
	}
	for (secId = 0; secId <= MAX_SECTOR; secId++) {
		if (sectors[secId].sector < 255) {
			printf("%d/%d%c:\n", sectors[secId].track, sectors[secId].sector & 0x7f, matched[secId] ? '+' : ' ');

			for (int i = 0; i < 128; i += 16) {
				for (int j = i; j < i + 16; j++)
					printf("%02X ", sectors[secId].data[j]);

				for (int j = i; j < i + 16; j++)
					printf("%c", (' ' <= sectors[secId].data[j] && sectors[secId].data[j] <= '~') ? sectors[secId].data[j] : '.');
				putchar('\n');
			}
			printf("forward %d/%d backward %d/%d crc %02X%02X\n\n", sectors[secId].ftrack, sectors[secId].fsector,
				sectors[secId].btrack, sectors[secId].bsector,
				sectors[secId].crc1, sectors[secId].crc2);

		}
		else
			printf("failed to extract sector %d\n", secId);
	}

}


#if 0
tofix()
{
	for (int i = 0; i < (rcnt < 128 ? rcnt : 128); i += 16) {
		for (int j = i; j < i + 16; j++)
			printf("%02X ", data[j + 2]);
		for (int j = i; j < i + 16; j++)
			printf("%c", ' ' <= data[j + 2] && data[j + 2] <= '~' ? data[j + 2] : '.');
		putchar('\n');
	}
	if (rcnt >= 133)
		printf("fsector = %d, ftrack = %d, bsector = %d, btrack = %d\n", data[130], data[131], data[132], data[133]);

	if (rcnt >= 135) {
		printf("crc1 = %02X, crc2 = %02X\n\n", data[134], data[135]);
		crcCheck(data, 136);
	}
	if (rcnt < 137 || data[136] != 0 || data[137] != 0)
		printf("incomplete or invalid postamble\n");

}

}
#endif

#define BITS_PER_LINE   80

int bitLog(int bits)
{
	static int bitCnt;
	static char bitBuf[BITS_PER_LINE + 1];
	if (debug > VERYVERBOSE) {
		if (bits == BITSTART)
			bitCnt = 0;
		else if (bitCnt && (bitCnt == BITS_PER_LINE || bits == BITFLUSH)) {
			printf("%.*s\n", bitCnt, bitBuf);
			bitCnt = 0;
		}

		if (BIT0 <= bits && bits <= BITBAD) {
			bitBuf[bitCnt++] = "0m1MsSEB"[bits - 1];
			if (bits >= BIT0S) {                // resync or bad causes flush
				printf("%.*s\n", bitCnt, bitBuf);
				bitCnt = 0;
			}
		}
	}
	return bits;
}



/* some simplifications as markers are not used for ZDS disks
   hence there should be a clock every ~ 4us and possibly an interleaved data pulse
   at around 2us
*/
static bool synced = false;
static int syncPeriod = RESYNCTIME;
static int syncCnt;

void resetGetFMBit(int resync, int initSync)
{
	synced = false;
	syncPeriod = resync;
	syncCnt = initSync;
}

double newClock(int cnt, double oldClock)
{
	int ticks = cnt / oldClock + 0.5;
//	printf("%f\n", (double)cnt / ticks);
	return (double)cnt / ticks;
}

int getFMBit()
{
	static bool cbit = false;
	static double clock;
	static int val;
	static int resyncCnt;

	if (!synced) {
		int zeroCnt = 0;
		resyncCnt = 0;

		clock = USCLOCK;
		while (1) {
			if ((val = getNextFlux()) == END_BLOCK || val == END_FLUX)
				return bitLog(BITEND);
			if (val < 3 * clock || val > 5 * clock)
				zeroCnt = resyncCnt = 0;
			else {
				resyncCnt += val;
				if (++zeroCnt == syncCnt) {
					clock = resyncCnt / (4.0 * zeroCnt);
					cbit = true;
					resyncCnt = 0;
					synced = true;
					break;
				}
			}
		}
	}

	while (1) {
		while (val < clock) {
			int newval = getNextFlux();
			if (newval == END_BLOCK || newval == END_FLUX)
				return bitLog(BITEND);
			if ((resyncCnt += newval) > syncPeriod * clock) {
				clock = newClock(resyncCnt, clock);
				resyncCnt = 0;
			}
			val += newval;
		}

		if (val < (3 * clock)) {	// either d or c bit
			val = 0;
			if (!(cbit = !cbit))
				return bitLog(BIT1);
		}
		else if (val < (5 * clock)) {		// missing bit ok for cbit
			val = 0;
			return bitLog(cbit ? BIT0 : BIT1);
		}
		else if (cbit) {			// missing clock bit
			val -= (4 * clock + 0.5);
			return bitLog(BIT0);
		}
		else {
			cbit = true;
			val -= (2 * clock + 0.5);
		}


	}
}



bool analyseFormat()
{
	curFmt = formats;	// only one

	return true;
}