#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <cstdio>
#include <cinttypes>
#include <unistd.h>
#include <wiiuse/wpad.h>
#include <gccore.h>
#include <ogcsys.h>
#include <fat.h>

#include "mos6502.h"

#define DBGMODE 0

std::vector<uint8_t> AddressSpace(65536);
std::vector<uint8_t> PPUAddressSpace(16384);
uint16_t prgstartlocation;
std::vector<uint8_t> prgromData;
int prgRomSize;
int chrRomSize;
int usesChrRam;
int hMirrored;
int usesBatt;
int gamePlaying = 0;

uint16_t ppuVRAMAddress = 0x0000;
uint8_t ppuVRAMAddressP1 = 0x00;
uint8_t ppuVRAMAddressP2 = 0x00;
int whichVRAMAddrPart = 0;
uint8_t ppuStatusRegister = 0x00;
int isVBlank = 0;
int readyforFBDraw = 0;

int rows, cols, v;

int curbuf = 0;

// Pee pee you.
uint8_t palette[][3] = {
    {0x65, 0x65, 0x65},
    {0x00, 0x12, 0x7D},
    {0x18, 0x00, 0x8E},
    {0x36, 0x00, 0x82},
    {0x56, 0x00, 0x5D},
    {0x5A, 0x00, 0x18},
    {0x4F, 0x05, 0x00},
    {0x38, 0x19, 0x00},
    {0x1D, 0x31, 0x00},
    {0x00, 0x3D, 0x00},
    {0x00, 0x41, 0x00},
    {0x00, 0x3B, 0x17},
    {0x00, 0x2E, 0x55},
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0xAF, 0xAF, 0xAF},
    {0x19, 0x4E, 0xC8},
    {0x47, 0x2F, 0xE3},
    {0x6B, 0x1F, 0xD7},
    {0x93, 0x1B, 0xAE},
    {0x9E, 0x1A, 0x5E},
    {0x99, 0x32, 0x00},
    {0x7B, 0x4B, 0x00},
    {0x5B, 0x67, 0x00},
    {0x26, 0x7A, 0x00},
    {0x00, 0x82, 0x00},
    {0x00, 0x7A, 0x3E},
    {0x00, 0x6E, 0x8A},
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0xFF, 0xFF, 0xFF},
    {0x64, 0xA9, 0xFF},
    {0x8E, 0x89, 0xFF},
    {0xB6, 0x76, 0xFF},
    {0xE0, 0x6F, 0xFF},
    {0xEF, 0x6C, 0xC4},
    {0xF0, 0x80, 0x6A},
    {0xD8, 0x98, 0x2C},
    {0xB9, 0xB4, 0x0A},
    {0x83, 0xCB, 0x0C},
    {0x5B, 0xD6, 0x3F},
    {0x4A, 0xD1, 0x7E},
    {0x4D, 0xC7, 0xCB},
    {0x4C, 0x4C, 0x4C},
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00},
    {0xFF, 0xFF, 0xFF},
    {0xC7, 0xE5, 0xFF},
    {0xD9, 0xD9, 0xFF},
    {0xE9, 0xD1, 0xFF},
    {0xF9, 0xCE, 0xFF},
    {0xFF, 0xCC, 0xF1},
    {0xFF, 0xD4, 0xCB},
    {0xF8, 0xDF, 0xB1},
    {0xED, 0xEA, 0xA4},
    {0xD6, 0xF4, 0xA4},
    {0xC5, 0xF8, 0xB8},
    {0xBE, 0xF6, 0xD3},
    {0xBF, 0xF1, 0xF1},
    {0xB9, 0xB9, 0xB9},
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00}
};

static void *conxfb = nullptr;
u32 *axfb = nullptr;
u32 *bxfb = nullptr;
static GXRModeObj *rmode = nullptr;

// Used for framebuffer draw handling. Not Facebook.
// Sorry, Susan. You're not reading Facebook on your Wii. 
static lwp_t fb_handle = (lwp_t)nullptr;

// Read function for the emulator
uint8_t Read(uint16_t address) {
    if(DBGMODE) printf("Read: 0x%04X\n", address);
    // I know we could write to memory. Screw that.
    if(address == 0x2002) return ppuStatusRegister;
    ppuStatusRegister &= ~(1 << 7);
    return AddressSpace[address];
}

u32 CvtRGB(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2) {
    int y1, cb1, cr1, y2, cb2, cr2, cb, cr;

    y1 = (299 * r1 + 587 * g1 + 114 * b1) / 1000;
    cb1 = (-16874 * r1 - 33126 * g1 + 50000 * b1 + 12800000) / 100000;
    cr1 = (50000 * r1 - 41869 * g1 - 8131 * b1 + 12800000) / 100000;

    y2 = (299 * r2 + 587 * g2 + 114 * b2) / 1000;
    cb2 = (-16874 * r2 - 33126 * g2 + 50000 * b2 + 12800000) / 100000;
    cr2 = (50000 * r2 - 41869 * g2 - 8131 * b2 + 12800000) / 100000;

    cb = (cb1 + cb2) >> 1;
    cr = (cr1 + cr2) >> 1;

    return (y1 << 24) | (cb << 16) | (y2 << 8) | cr;
}

// Write function for the emulator
void Write(uint16_t address, uint8_t value) {
    if(DBGMODE) printf("Write: 0x%04X, 0x%02X\n", address, value);
    if(address == 0x2006) {
        if(whichVRAMAddrPart == 0) {
            ppuVRAMAddressP1 = value;
            ppuVRAMAddress = ((uint16_t)ppuVRAMAddressP1 << 8) | ppuVRAMAddressP2;
            whichVRAMAddrPart = 1;
        } else {
            ppuVRAMAddressP2 = value;
            ppuVRAMAddress = ((uint16_t)ppuVRAMAddressP1 << 8) | ppuVRAMAddressP2;
            whichVRAMAddrPart = 0;
        }
    }
    if(address == 0x2007) {
        PPUAddressSpace[ppuVRAMAddress] = value;
        if(AddressSpace[0x2000] & (1 << 2)) ppuVRAMAddress += 32;
        else ppuVRAMAddress += 1;
        v = (0 * 320) + (0 >> 1);
        for (rows = 0; rows < 640; rows++) {
		for (cols = 0; cols < 480; cols++)
			if(curbuf == 0) axfb[v + cols] = CvtRGB(palette[PPUAddressSpace[0x3F00]][0], palette[PPUAddressSpace[0x3F00]][1], palette[PPUAddressSpace[0x3F00]][2], palette[PPUAddressSpace[0x3F00]][0], palette[PPUAddressSpace[0x3F00]][1], palette[PPUAddressSpace[0x3F00]][2]);
                        else bxfb[v + cols] = CvtRGB(palette[PPUAddressSpace[0x3F00]][0], palette[PPUAddressSpace[0x3F00]][1], palette[PPUAddressSpace[0x3F00]][2], palette[PPUAddressSpace[0x3F00]][0], palette[PPUAddressSpace[0x3F00]][1], palette[PPUAddressSpace[0x3F00]][2]);
		v += 320;
        }
        readyforFBDraw = 1;
    }
    AddressSpace[address] = value;
}

void initconsole() {
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(nullptr);
    conxfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(conxfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_SetNextFramebuffer(conxfb);
    VIDEO_Configure(rmode);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
}

// Framebook- I mean, framebuffer render thread
void *fbrenderthread(void *arg) {
    while(1) {
        if(readyforFBDraw == 1) {
            // Double buffering. Smooth, not crap.
            // Framebuffer A
            if(curbuf == 0) VIDEO_SetNextFramebuffer(axfb);
            // Framebuffer B
            if(curbuf == 1) VIDEO_SetNextFramebuffer(bxfb);
            VIDEO_Flush();
            VIDEO_WaitVSync();
            readyforFBDraw = 0;
            // Swap framebuffers for double buffering
            if(curbuf == 0) curbuf = 1;
            else if(curbuf == 1) curbuf = 0;
        }
        WPAD_ScanPads();
        if(WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME) exit(0);
        // Single file, sharing is caring, and you need to share the Broadway.
        LWP_YieldThread();
    }
    return nullptr;
}

void initgraphics() {
    axfb = (u32 *)MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    bxfb = (u32 *)MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    // Clear frame buffers
    VIDEO_ClearFrameBuffer(rmode, axfb, COLOR_BLACK);
    VIDEO_ClearFrameBuffer(rmode, bxfb, COLOR_BLACK);
    VIDEO_SetNextFramebuffer(axfb);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    LWP_CreateThread(&fb_handle, fbrenderthread, nullptr, nullptr, 64*1024, 50);
    std::cout << "Initialized graphics.\n";
}

void initinput() {
    WPAD_Init();
    PAD_Init();
}

std::vector<uint8_t> readfile(const std::string &filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Unable to read " << filename << std::endl;
        return {};
    }

    file.seekg(0, std::ios::end);
    std::streampos file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(file_size);
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);

    return buffer;
}

void Start2A03Emulation() {
    printf("[--( Stage 3: Hardware Emulation )------------------------------------------]");
    //initgraphics();
    mos6502 cpu(Read, Write);
    cpu.Reset();
    struct timespec ts;
    ts.tv_sec = 0;
    // OK, Perfectionist Joe. I know it's slower than 1.79 MHz.
    // Who's gonna stop a tired guy infront of a stupid screen?
    // That's me, by the way.
    int cyclecounter = 0;
    gamePlaying = 1;
    ts.tv_nsec = 559; // 1.788908765653 MHz. ^_^
    while (gamePlaying) {
        // I'm not sure what this 64 bit integer does.
        // Edit: Now I do.
        uint64_t cycleCount = 0;
        cpu.Run(1, cycleCount);
        // printf("%llu\n", cycleCount);
        cyclecounter += cycleCount;
        // It's vblank'n time.
        // P.S. This is inefficient and inaccurate.
        // But it works, so shut up. 
        if(cyclecounter >= 27360) {
            isVBlank = 1;
            ppuStatusRegister |= (1 << 7);
            cyclecounter = 0;
            if(DBGMODE) printf("VBlank flag on\n");
        }
        if(isVBlank == 1 && cyclecounter >= 2280) {
            isVBlank = 0;
            ppuStatusRegister &= ~(1 << 7);
            cyclecounter = 0;
            if(DBGMODE) printf("VBlank flag off\n");
        }
        ts.tv_nsec = (559*cycleCount); // 1.788908765653 MHz. ^_^
        nanosleep(&ts, nullptr);
    }
}

void insert_at_location(std::vector<uint8_t>& dest, const std::vector<uint8_t>& src) {
    // Ensure the destination vector has at least 0xFFFF + 1 elements
    if (dest.size() <= 0xFFFF) {
        dest.resize(0xFFFF + 1);  // Make sure there's space till index 0xFFFF
    }
    
    // Calculate where the source vector should be placed
    size_t insert_pos = 0xFFFF - src.size() + 1;
    
    // Insert the source vector into the destination vector at the calculated position
    dest.insert(dest.begin() + insert_pos, src.begin(), src.end());
}

void InitEmulation() {
    printf("[--( Stage 2: Initializing Emulation )--------------------------------------]");
    printf("Preparing 2A03 emulation...\n");

    // Don't flippin bug me into adding MMC emulation this early.
    if (prgRomSize > 32768) {
        printf("PRG ROM too big, cannot copy into address space.\n");
        while(1);
    }
    // Insert the "totally non-pirated ROM of SMB2" into memory
    insert_at_location(AddressSpace, prgromData);
    Start2A03Emulation();
}

void LoadROM(const char* romPath, int skip, int getInfo) {
    printf("[--( Stage 1: Loading ROM )-------------------------------------------------]");
    printf("Opening %s...\n", romPath);

    if (skip) {
        printf("Skipping, as reset command has been sent.\n");
        InitEmulation();
    }

    FILE* file = fopen(romPath, "rb");
    if (!file) {
        printf("Failed to open %s!\n", romPath);
        sleep(3);
        exit(1);
    }

    unsigned char header[16];
    fread(header, 1, 16, file);

    if (header[0] != 'N' || header[1] != 'E' || header[2] != 'S' || header[3] != 0x1A) {
        printf("Invalid iNES file. iNES files are .nes files.\n");
        sleep(3);
        exit(1);
    }

    printf("ROM opened successfully!\n");

    prgRomSize = 16384 * header[4];
    printf("PRG ROM size: %dKB\n", 16 * header[4]);

    if ('0' == '1') {
        printf("Game uses battery\n");
    } else {
        usesBatt = 0;
        printf("Game has no battery\n");
    }

    if (!getInfo) {
        prgromData = std::vector<uint8_t>(prgRomSize);
        fseek(file, 16, SEEK_SET);
        fread(prgromData.data(), 1, prgRomSize, file);
        InitEmulation();
    }

    fclose(file);
}

int main() {
    initconsole();
    printf("NotNES by IMakeWii\n");

    if (!fatInitDefault()) {
        printf("Failed to init libfat\n");
        sleep(3);
        exit(1);
    }
    printf("Initialized libfat\n");
    initinput();
    printf("Initialized input\n");

    LoadROM("sd:/notnes/roms/example.nes", 0, 0);

    return 0;
}
