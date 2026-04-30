#include <stdlib.h>
#include <stdio.h>
#include <dos.h>
#include <conio.h>
#include <string.h>

#define Byte unsigned char
#define uint unsigned int
#define ulong unsigned long
#define bool char
#define true -1
#define false 0

#define MakeFP(seg, ofs) ((void far*)(((ulong)(seg) << 16) | (ulong)(ofs)))
#define GetSeg(fp) (uint)(((ulong)((void far*)&(fp)) & 0xffff0000)>>16)
#define GetOff(fp) (uint)((ulong)((void far*)&(fp)) & 0xffff)

#define ToInt32(hi, lo) ((ulong)(((ulong)(hi) << 16) | (ulong)(lo)))

#define ScrPosToAddr(x, y) (((y) * 160) + ((x) * 2))

// Global variables
bool soundTable = false;
bool newInt8 = false;
bool fastWorker = false;
Byte counter1 = 0;
Byte counter2 = 2;
bool HighSpeedTimer = false;

int (_cdecl far* InitSoundFn)();	// 0xa46
int (_cdecl far* PlayTuneFn)();	// 0xa4a
int (_cdecl far* CloseSoundFn)();	// 0xa4e
int (_cdecl far* SoundWorkerFn)();	// 0xa52
int (_cdecl far* FastSoundWorkerFn)();	// 0xa56
int (_cdecl far* SoundTimerFn)();	// 0xa5a

void (_cdecl _interrupt far* OldInt8)();

void PrintString(int xPos, int yPos, Byte attrib, char* text);

int far EmptyFn()
{
	return 0;
}

void _interrupt far NewInt8()
{
	int value;

	if (HighSpeedTimer)
	{
		_asm sti

		if (fastWorker)
		{
			FastSoundWorkerFn();
		}

		counter1--;

		if (counter1 == 0)
		{
			counter1 = 5;
			
			value = SoundWorkerFn();

			if (value != 0)
			{
				fastWorker = value != 0;
			}
		}

		counter2--;
		if (counter2 != 0)
		{
			_asm cli
			_asm mov al, 0x20
			_asm out 0x20, al
		}
		else
		{
			counter2 = 0x10;

			_chain_intr(OldInt8);
		}
	}
	else
	{
		_chain_intr(OldInt8);
	}
}

void InitNewInt8()
{
	if (!newInt8)
	{
		OldInt8 = _dos_getvect(8);
		_dos_setvect(8, NewInt8);

		// Initialize timer
		_asm cli
		_asm mov al, 0x36
		_asm out 0x43, al
		_asm mov al, 0x88;
		_asm out 0x40, al
		_asm mov al, 0xf
		_asm out 0x40, al
		HighSpeedTimer = true;
		_asm sti

		newInt8 = true;
		fastWorker = false;
	}
}

void RestoreOldInt8()
{
	if (newInt8)
	{
		fastWorker = false;

		// Restore timer
		_asm cli
		_asm mov al, 0x36
		_asm out 0x43, al
		_asm mov al, 0
		_asm out 0x40, al
		_asm mov al, 0
		_asm out 0x40, al
		HighSpeedTimer = false;
		_asm sti

		_dos_setvect(8, OldInt8);

		newInt8 = false;
	}
}

void far LoadOverlayTable(uint segment)
{
	int i;

	if (segment != 0)
	{
		uint far* overlayTable = (uint far*)MakeFP(segment, 0x32);
		uint ovarlayCS = *((uint far*)MakeFP(segment, 0x28));

		InitSoundFn = (int (far*)())ToInt32(ovarlayCS, overlayTable[0]);
		PlayTuneFn = (int (far*)())ToInt32(ovarlayCS, overlayTable[1]);
		CloseSoundFn = (int (far*)())ToInt32(ovarlayCS, overlayTable[2]);
		SoundWorkerFn = (int (far*)())ToInt32(ovarlayCS, overlayTable[3]);
		FastSoundWorkerFn = (int (far*)())ToInt32(ovarlayCS, overlayTable[4]);
		SoundTimerFn = (int (far*)())ToInt32(ovarlayCS, overlayTable[5]);

		soundTable = true;

		InitNewInt8();
	}
}

uint far LoadOverlay(char far* filename)
{
	union REGS regs;
	struct SREGS sregs;
	uint maxFreeSeg;
	uint memSegment = 0;
	uint overlayBlock[4];

	regs.x.bx = 0xffff;
	regs.h.ah = 0x48;
	int86(0x21, &regs, &regs);	// Allocate Memory; get maximum free block count
	if (regs.x.ax == 8 && regs.x.cflag != 0)
	{
		maxFreeSeg = regs.x.bx;
		maxFreeSeg -= 2;

		regs.x.bx = maxFreeSeg;
		regs.h.ah = 0x48;
		int86(0x21, &regs, &regs);
		if (regs.x.cflag == 0)
		{
			memSegment = regs.x.ax;

			overlayBlock[0] = memSegment;
			overlayBlock[1] = memSegment;

			sregs.ds = FP_SEG(filename);
			regs.x.dx = FP_OFF(filename);

			sregs.es = GetSeg(overlayBlock);
			regs.x.bx = GetOff(overlayBlock);

			regs.h.ah = 0x4b;
			regs.h.al = 0x3;
			int86x(0x21, &regs, &regs, &sregs);	// EXEC/Load and Execute Program
			if (regs.x.cflag == 0)
			{
				ulong segment = *((uint far*)MakeFP(overlayBlock[0], 0x2a));
				ulong size = *((uint far*)MakeFP(overlayBlock[0], 0x2c));

				size += 0xf;
				size >>= 4;
				segment += size;
				segment -= overlayBlock[0];
				segment += 0x8;
				if (segment < maxFreeSeg)
				{
					sregs.es = overlayBlock[0];
					regs.x.bx = segment;
					regs.h.ah = 0x4a;
					int86x(0x21, &regs, &regs, &sregs);	// Modify Allocated Memory Block
					if (regs.x.cflag == 0)
					{
						LoadOverlayTable(overlayBlock[0]);

						return overlayBlock[0];
					}
				}
			}
		}
	}

	if (memSegment != 0)
	{
		sregs.es = memSegment;
		regs.h.ah = 0x49;
		int86x(0x21, &regs, &regs, &sregs);	// Free Allocated Memory
	}

	return 0;
}

void far RestoreOverlayTable()
{
	if (soundTable)
	{
		InitSoundFn = EmptyFn;
		PlayTuneFn = EmptyFn;
		CloseSoundFn = EmptyFn;
		SoundWorkerFn = EmptyFn;
		FastSoundWorkerFn = EmptyFn;
		SoundTimerFn = EmptyFn;

		soundTable = false;
	}
}

void far FreeOverlay(uint segment)
{
	// function body
	union REGS regs;
	struct SREGS sregs;

	if (segment != 0)
	{
		RestoreOldInt8();
		RestoreOverlayTable();

		sregs.es = segment;
		regs.h.ah = 0x49;
		int86x(0x21, &regs, &regs, &sregs);	// Free Allocated Memory
	}
}

uint SoundCardMenu()
{
	char* filename = "?SOUND.CVL";
	bool hasFileName = false;
	int ch;
	int i;
	char choice[10];

	printf("DOS Civilization (1991) music player by R. Horvat\n");
	printf("\n");
	printf("A = Adlib or compatibles\n");
	printf("G = General MIDI sound driver\n");
	printf("I = IBM speaker\n");
	printf("P = Pro cards (OPL-3) driver\n");
	printf("R = Roland MT-32/LAPC-1\n");
	printf("T = Tandy sound\n");
	printf("\n");
	printf("Q = Quit\n");
	printf("\n");

	do
	{
		for (i = 0; i < 10; i++)
		{
			choice[i] = 0;
		}

		printf("Which sound card do you have: ");
		scanf("%c", choice);

		ch = choice[0];

		switch (ch)
		{
		case 'A':
		case 'a':
			filename[0] = 'A';
			hasFileName = true;
			break;

		case 'G':
		case 'g':
			filename[0] = 'G';
			hasFileName = true;
			break;

		case 'I':
		case 'i':
			filename[0] = 'I';
			hasFileName = true;
			break;

		case 'P':
		case 'p':
			filename[0] = 'P';
			hasFileName = true;
			break;

		case 'Q':
		case 'q':
			return 0xffff;

		case 'R':
		case 'r':
			filename[0] = 'R';
			hasFileName = true;
			break;

		case 'T':
		case 't':
			filename[0] = 'T';
			hasFileName = true;
			break;

		default:
			fflush(stdin);
			printf("\r");
			break;
		}
	} while (!hasFileName);

	return LoadOverlay(filename);
}

int main()
{
	uint overlaySeg;
	int tune;

	overlaySeg = SoundCardMenu();

	if (overlaySeg == 0xffff)
	{
		return 0;
	}
	else if (overlaySeg == 0)
	{
		printf("Not enough memory to load sound overlay or overlay missing!");

		return 1;
	}
	else
	{
		if (InitSoundFn(0, 0, 0, 0, 0, 0, 0) != 0)
		{
			printf("Could not detect selected sound card. Please check your settings and try again\n");

			FreeOverlay(overlaySeg);

			return 1;
		}
		else
		{
			do
			{
				tune = -1;

				printf("\nPlease enter the tune number (3-44, 0 to exit): ");
				scanf("%d", &tune);
				fflush(stdin);

				if (tune == 0)
				{
					break;
				}
				else if (tune > 2 && tune < 45)
				{
					PlayTuneFn(tune, 3); // 3 has been here as a default
					printf("Press any key to stop playing\n");
					getch();

					PlayTuneFn(0);
				}
			} while (true);

			PlayTuneFn(0);

			_asm push si
			_asm push di
			CloseSoundFn();
			_asm pop di
			_asm pop si

			FreeOverlay(overlaySeg);
		}
	}

	return 0;
}
