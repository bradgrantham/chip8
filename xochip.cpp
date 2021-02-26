#include <algorithm>
#include <unordered_map>
#include <map>
#include <string>
#include <array>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <chrono>
#include <random>

#ifdef __APPLE__
#define XCODE_MISSING_FILESYSTEM_FOR_YEARS
#include <libgen.h>
#else
#include <filesystem>
#endif

#include <MiniFB.h>

constexpr int DEBUG_STATE = 0x01;
constexpr int DEBUG_ASM = 0x02;
constexpr int DEBUG_DRAW = 0x04;
constexpr int DEBUG_FAIL_UNSUPPORTED_INSN = 0x08;
constexpr int DEBUG_KEYS = 0x10;
std::unordered_map<std::string, int> keywordsToDebugFlags = {
    {"state", DEBUG_STATE},
    {"asm", DEBUG_ASM},
    {"draw", DEBUG_DRAW},
    {"insn", DEBUG_FAIL_UNSUPPORTED_INSN},
    {"keys", DEBUG_KEYS},
};
int debug = 0;

constexpr uint32_t QUIRKS_NONE = 0x00;
constexpr uint32_t QUIRKS_SHIFT = 0x01;           /* shift VX instead of VY */
constexpr uint32_t QUIRKS_LOAD_STORE = 0x02;      /* don't add X + 1 to I */
constexpr uint32_t QUIRKS_JUMP = 0x04;            /* VX is used as offset *and* X used as address high nybble */
constexpr uint32_t QUIRKS_CLIP = 0x08;            /* no draw or collide wrapped, VX += rows off bottom */
constexpr uint32_t QUIRKS_VFORDER = 0x10;         /* VF is set first in ADD, SUB, SH ALU operations */
constexpr uint32_t QUIRKS_LOGIC = 0x20;           /* VF is cleared after logic ALU operations */

enum ChipPlatform
{
    CHIP8,
    SCHIP_1_1,
    XOCHIP
};

enum DisplayRotation
{
    ROT_0, ROT_90, ROT_180, ROT_270
};

void disassemble(uint16_t pc, uint16_t instructionWord, uint16_t wordAfter);

template <class MEMORY, class INTERFACE>
struct Chip8Interpreter
{
    ChipPlatform platform;
    uint32_t quirks;

    uint64_t clock = 0;

    std::array<uint8_t, 16> registers = {0};
    std::array<uint8_t, 8> RPL = {0};
    std::vector<uint16_t> stack;
    uint16_t I = 0;
    uint16_t pc = 0;
    uint8_t DT = 0;
    uint8_t ST = 0;
    bool extendedScreenMode = false;
    uint32_t screenPlaneMask = 0x1;

    std::random_device r;
    std::default_random_engine e1;
    std::uniform_int_distribution<int> uniform_dist;

    bool waitingForKeyPress = false;
    bool waitingForKeyRelease = false;
    uint8_t keyPressed;
    int keyDestinationRegister;

    Chip8Interpreter(uint16_t initialPC, ChipPlatform platform, uint32_t quirks) :
        platform(platform),
        quirks(quirks),
        pc(initialPC),
        e1(r()),
        uniform_dist(0, 255)
    {
    }

    void tick(INTERFACE& interface)
    {
        if(DT > 0) {
            DT--;
        }
        if(ST > 0) {
            ST--;
            if(ST == 0) {
                interface.stopSound();
            }
        }
    }

    enum InstructionHighNybble
    {
        INSN_SYS = 0x0,
        INSN_JP = 0x1,
        INSN_CALL = 0x2,
        INSN_SE_IMM = 0x3,
        INSN_SNE_IMM = 0x4,
        INSN_HIGH5 = 0x5,
        INSN_LD_IMM = 0x6,
        INSN_ADD_IMM = 0x7,
        INSN_ALU = 0x8,
        INSN_SNE_REG = 0x9,
        INSN_LD_I = 0xA,
        INSN_JP_V0 = 0xB,
        INSN_RND = 0xC,
        INSN_DRW = 0xD,
        INSN_SKP = 0xE,
        INSN_LD_SPECIAL = 0xF,
    };

    enum Series5Opcode // 5XYN low nybble
    {
        HIGH5_SE_REG = 0x0,
        HIGH5_LD_I_VXVY = 0x2,
        HIGH5_LD_VXVY_I = 0x3,
    };

    enum SYSOpcode
    {
        SYS_CLS = 0x0E0,
        SYS_RET = 0x0EE,
        SYS_SCROLL_DOWN = 0x0C0,
        SYS_SCROLL_UP = 0x0D0,
        SYS_SCROLL_RIGHT_4 = 0xFB,
        SYS_SCROLL_LEFT_4 = 0xFC,
        SYS_EXIT = 0xFD,
        SYS_ORIGINAL_SCREEN = 0xFE,
        SYS_EXTENDED_SCREEN = 0xFF,
    };

    enum SPECIALOpcode
    {
        SPECIAL_GET_DELAY = 0x07,
        SPECIAL_KEYWAIT = 0x0A,
        SPECIAL_SET_DELAY = 0x15,
        SPECIAL_SET_SOUND = 0x18,
        SPECIAL_ADD_INDEX = 0x1E,
        SPECIAL_LD_DIGIT = 0x29,
        SPECIAL_LD_BCD = 0x33,
        SPECIAL_LD_IVX = 0x55,
        SPECIAL_LD_VXI = 0x65,
        SPECIAL_STORE_RPL = 0x75,
        SPECIAL_LD_RPL = 0x85,
        SPECIAL_LD_BIGDIGIT = 0x30,
        SPECIAL_LD_I_16BIT = 0x00,
        SPECIAL_SET_PLANES = 0x01,
        SPECIAL_SET_AUDIO = 0x02,
    };

    enum SKPOpcode {
        SKP_KEY = 0x9E,
        SKNP_KEY = 0xA1,
    };

    enum ALUOpcode {
        ALU_LD = 0x0,
        ALU_OR = 0x1,
        ALU_AND = 0x2,
        ALU_XOR = 0x3,
        ALU_ADD = 0x4,
        ALU_SUB = 0x5,
        ALU_SHR = 0x6,
        ALU_SUBN = 0x7,
        ALU_SHL = 0xE,
    };

    enum StepResult {
        CONTINUE,
        EXIT_INTERPRETER,
        UNSUPPORTED_INSTRUCTION,
    };

    uint16_t readU16(MEMORY& memory, uint16_t addr)
    {
        uint8_t hiByte = memory.read(addr);
        uint8_t loByte = memory.read(addr + 1);
        return hiByte * 256 + loByte;
    }

    int getInstructionSize(MEMORY& memory, uint16_t addr)
    {
        if(platform == XOCHIP) {
            if(readU16(memory, addr) == 0xF000) {
                return 4;
            } else {
                return 2;
            }
        } else {
            return 2;
        }
    }

    void storeALUResult(int destination, uint8_t result, bool f)
    {
        if(quirks & QUIRKS_VFORDER) {
            registers[0xF] = f ? 1 : 0;
            registers[destination] = result;
        } else {
            registers[destination] = result;
            registers[0xF] = f ? 1 : 0;
        }
    }

    StepResult step(MEMORY& memory, INTERFACE& interface)
    {
        StepResult stepResult = CONTINUE;
        uint16_t instructionWord = readU16(memory, pc);
        uint8_t imm8Argument = instructionWord & 0x00FF;
        uint8_t imm4Argument = instructionWord & 0x000F;
        uint16_t imm12Argument = instructionWord & 0x0FFF;
        uint16_t xArgument = (instructionWord & 0x0F00) >> 8;
        uint16_t yArgument = (instructionWord & 0x00F0) >> 4;
        int highNybble = instructionWord >> 12;

        if(waitingForKeyPress) {

            bool isPressed = false;
            uint8_t whichKey;

            for(uint8_t i = 0; i < 16; i++) {
                if(interface.pressed(i)) {
                    isPressed = true;
                    whichKey = i;
                }
            }

            if(isPressed) {
                if(debug & DEBUG_KEYS) {
                    printf("pressed %d now wait for release\n", whichKey);
                }
                keyPressed = whichKey;
                waitingForKeyPress = false;
                waitingForKeyRelease = true;
            } else {
                return CONTINUE;
            }
        }

        if(waitingForKeyRelease) {
            bool wasReleased = !interface.pressed(keyPressed);

            if(wasReleased) {
                if(debug & DEBUG_KEYS) {
                    printf("key wait over\n");
                }
                waitingForKeyRelease = false;
                registers[keyDestinationRegister] = keyPressed;
            } else {
                return CONTINUE;
            }
        }

        if(debug & DEBUG_STATE) {
            printf("CHIP8: clk:%llu pc:%04X I:%04X ", clock++, pc, I);
            for(int i = 0; i < 16; i++) {
                printf("%02X ", registers[i]);
            }
            puts("");
        }

        if(debug & DEBUG_ASM) {
            uint16_t wordAfter = readU16(memory, pc + 2);
            disassemble(pc, instructionWord, wordAfter);
        }

        if(false) {
            if(false && (clock >= 43757)) {
                for(int row = 0; row < 32; row++) {
                    for(int col = 0; col < 64; col++) {
                        printf("%c", interface.display.at(row * 2).at(col * 2) ? '#' : '.');
                    }
                    puts("");
                }
            }
        }

        uint16_t nextPC = pc + getInstructionSize(memory, pc);

        switch(highNybble) {
            case INSN_SYS: {
                uint16_t sysOpcode = instructionWord & 0xFFF;
                switch(sysOpcode) {
                    case SYS_CLS: { // 00E0 - CLS - Clear the display.
                        interface.clear();
                        break;
                    }
                    case SYS_RET: { //  00EE - RET - Return from a subroutine.  The interpreter sets the program counter to the address at the top of the stack, then subtracts 1 from the stack pointer.
                        nextPC = stack.back();
                        stack.pop_back();
                        break;
                    }
                    case SYS_SCROLL_RIGHT_4: { // 00FB*    Scroll display 4 pixels right
                        if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
                            interface.scroll(-4, 0);
                        } else {
                            fprintf(stderr, "unsupported 0XXX instruction %04X (SCROLL RIGHT 4) - does this ROM require \"schip\" platform?\n", instructionWord);
                            stepResult = UNSUPPORTED_INSTRUCTION;
                        }
                        break;
                    }
                    case SYS_SCROLL_LEFT_4: { // 00FC*    Scroll display 4 pixels left
                        if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
                            interface.scroll(4, 0);
                        } else {
                            fprintf(stderr, "unsupported 0XXX instruction %04X (SCROLL ELFT 4) - does this ROM require \"schip\" platform?\n", instructionWord);
                            stepResult = UNSUPPORTED_INSTRUCTION;
                        }
                        break;
                    }
                    case SYS_EXIT: { // 00FD*    Exit CHIP interpreter
                        if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
                            stepResult = EXIT_INTERPRETER;
                        } else {
                            fprintf(stderr, "unsupported 0XXX instruction %04X (EXIT) - does this ROM require \"schip\" platform?\n", instructionWord);
                            stepResult = UNSUPPORTED_INSTRUCTION;
                        }
                        break;
                    }
                    case SYS_EXTENDED_SCREEN: { // 00FF*    Enable extended screen mode for full-screen graphics
                        if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
                            extendedScreenMode = true;
                        } else {
                            fprintf(stderr, "unsupported 0XXX instruction %04X (EXTENDEDSCREEN) - does this ROM require \"schip\" platform?\n", instructionWord);
                            stepResult = UNSUPPORTED_INSTRUCTION;
                        }
                        break;
                    }
                    case SYS_ORIGINAL_SCREEN: { // 00FE*    Disable extended screen mode
                        if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
                            extendedScreenMode = false;
                        } else {
                            fprintf(stderr, "unsupported 0XXX instruction %04X (ORIGINALSCREEN) - does this ROM require \"schip\" platform?\n", instructionWord);
                            stepResult = UNSUPPORTED_INSTRUCTION;
                        }
                        break;
                    }
                    default : { // Opcode undefined or is a range
                        if((sysOpcode & 0xFF0) == SYS_SCROLL_UP) {
                            // scroll-up n (0x00DN) scroll the contents of the display up by 0-15 pixels.
                            if(platform == XOCHIP) {
                                interface.scroll(0, imm4Argument);
                            } else {
                                fprintf(stderr, "unsupported 0XXX instruction %04X (SCROLL UP) - does this ROM require \"xochip\" platform?\n", instructionWord);
                                stepResult = UNSUPPORTED_INSTRUCTION;
                            }
                        } else if((sysOpcode & 0xFF0) == SYS_SCROLL_DOWN) {
                            // 00CN*    Scroll display N lines down
                            if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
                                interface.scroll(0, -imm4Argument);
                            } else {
                                fprintf(stderr, "unsupported 0XXX instruction %04X (SCROLL DOWN) - does this ROM require \"schip\" platform?\n", instructionWord);
                                stepResult = UNSUPPORTED_INSTRUCTION;
                            }
                        } else {
                            fprintf(stderr, "%04X: unsupported 0NNN instruction %04X \n", pc, instructionWord);
                            stepResult = UNSUPPORTED_INSTRUCTION;
                        }
                        break;
                    }
                }
                break;
            }
            case INSN_JP: { // 1nnn - JP addr - Jump to location nnn.  The interpreter sets the program counter to nnn.
                nextPC = imm12Argument;
                break;
            }
            case INSN_CALL: { // 2nnn - CALL addr - Call subroutine at nnn.  The interpreter increments the stack pointer, then puts the current PC on the top of the stack. The PC is then set to nnn.
                stack.push_back(nextPC);
                nextPC = imm12Argument;
                break;
            }
            case INSN_SE_IMM: { // 3xkk - SE Vx, byte - Skip next instruction if Vx = kk.  The interpreter compares register Vx to kk, and if they are equal, increments the program counter by 2.
                if(registers[xArgument] == imm8Argument) {
                    nextPC = nextPC + getInstructionSize(memory, nextPC);
                }
                break;
            }
            case INSN_SNE_IMM: { // 4xkk - SNE Vx, byte - Skip next instruction if Vx != kk.  The interpreter compares register Vx to kk, and if they are not equal, increments the program counter by 2.
                if(registers[xArgument] != imm8Argument) {
                    nextPC = nextPC + getInstructionSize(memory, nextPC);
                }
                break;
            }
            case INSN_HIGH5: {
                uint8_t opcode = instructionWord & 0xF;
                switch(opcode) {
                    case HIGH5_LD_I_VXVY : { // save vx - vy (0x5XY2) save an inclusive range of registers to memory starting at i.
                        if(platform == XOCHIP) {
                            if(xArgument < yArgument) {
                                for(int i = 0; i <= yArgument - xArgument; i++) {
                                    memory.write(I + i, registers[xArgument + i]);
                                }
                            } else {
                                for(int i = 0; i <= xArgument - yArgument; i++) {
                                    memory.write(I + i, registers[xArgument - i]);
                                }
                            }
                        } else {
                            fprintf(stderr, "unsupported 0XXX instruction %04X (LD I Vx-Vy ) - does this ROM require \"xochip\" platform?\n", instructionWord);
                            stepResult = UNSUPPORTED_INSTRUCTION;
                        }
                        break;
                    }
                    case HIGH5_LD_VXVY_I : { // load vx - vy (0x5XY3) load an inclusive range of registers from memory starting at i.
                        if(platform == XOCHIP) {
                            if(xArgument < yArgument) {
                                for(int i = 0; i <= yArgument - xArgument; i++) {
                                    registers[xArgument + i] = memory.read(I + i);
                                }
                            } else {
                                for(int i = 0; i <= xArgument - yArgument; i++) {
                                    registers[xArgument - i] = memory.read(I + i);
                                }
                            }
                        } else {
                            fprintf(stderr, "unsupported 0XXX instruction %04X (LD I Vx-Vy ) - does this ROM require \"xochip\" platform?\n", instructionWord);
                            stepResult = UNSUPPORTED_INSTRUCTION;
                        }
                        break;
                    }
                    case HIGH5_SE_REG : { // 5xy0 - SE Vx, Vy - Skip next instruction if Vx = Vy.  The interpreter compares register Vx to register Vy, and if they are equal, increments the program counter by 2.
                        if(registers[xArgument] == registers[yArgument]) {
                            nextPC = nextPC + getInstructionSize(memory, nextPC);
                        }
                        break;
                    }
                    default : {
                        if(opcode != 0) {
                            fprintf(stderr, "%04X: unsupported instruction %04X\n", pc, instructionWord);
                            stepResult = UNSUPPORTED_INSTRUCTION;
                        }
                        break;
                    }
                }
                break;
            }
            case INSN_LD_IMM: { // 6xkk - LD Vx, byte - Set Vx = kk.  The interpreter puts the value kk into register Vx.  
                registers[xArgument] = imm8Argument;
                break;
            }
            case INSN_ADD_IMM: { // 7xkk - ADD Vx, byte - Set Vx = Vx + kk.  Adds the value kk to the value of register Vx, then stores the result in Vx.
                registers[xArgument] = registers[xArgument] + imm8Argument;
                break;
            }
            case INSN_ALU: {
                int opcode = instructionWord & 0x000F;
                switch(opcode) {
                    case ALU_LD: { // 8xy0 - LD Vx, Vy - Set Vx = Vy.  Stores the value of register Vy in register Vx.  
                        registers[xArgument] = registers[yArgument];
                        break;
                    }
                    case ALU_OR: { // 8xy1 - OR Vx, Vy - Set Vx = Vx OR Vy.
                        registers[xArgument] |= registers[yArgument];
                        if(quirks & QUIRKS_LOGIC) {
                            registers[0xF] = 0;
                        }
                        break;
                    }
                    case ALU_AND: { // 8xy2 - AND Vx, Vy - Set Vx = Vx AND Vy.
                        registers[xArgument] &= registers[yArgument];
                        if(quirks & QUIRKS_LOGIC) {
                            registers[0xF] = 0;
                        }
                        break;
                    }
                    case ALU_XOR: { // 8xy3 - XOR Vx, Vy -  Set Vx = Vx XOR Vy.
                        registers[xArgument] ^= registers[yArgument];
                        if(quirks & QUIRKS_LOGIC) {
                            registers[0xF] = 0;
                        }
                        break;
                    }
                    case ALU_ADD: { // 8xy4 - ADD Vx, Vy - Set Vx = Vx + Vy, set VF = carry.  The values of Vx and Vy are added together. If the result is greater than 8 bits (i.e., > 255,) VF is set to 1, otherwise 0. Only the lowest 8 bits of the result are kept, and stored in Vx.
                        uint8_t result = registers[xArgument] + registers[yArgument];
                        storeALUResult(xArgument, result, result > 0xFF);
                        break;
                    }
                    case ALU_SUB: { // 8xy5 - SUB Vx, Vy - Set Vx = Vx - Vy, set VF = NOT borrow.  If Vx > Vy, then VF is set to 1, otherwise 0. Then Vy is subtracted from Vx, and the results stored in Vx.
                        uint8_t result = registers[xArgument] - registers[yArgument];
                        storeALUResult(xArgument, result, registers[xArgument] >= registers[yArgument]);
                        break;
                    }
                    case ALU_SUBN: { // 8xy7 - SUBN Vx, Vy - Set Vx = Vy - Vx, set VF = NOT borrow.  If Vy > Vx, then VF is set to 1, otherwise 0. Then Vx is subtracted from Vy, and the results stored in Vx.
                        uint8_t result = registers[yArgument] - registers[xArgument];
                        storeALUResult(xArgument, result, registers[yArgument] >= registers[xArgument]);
                        break;
                    }
                    case ALU_SHR: { // 8xy6 - SHR Vx {, Vy} - Set Vx = Vy SHR 1.  If the least-significant bit of Vy is 1, then VF is set to 1, otherwise 0. Then Vx is Vy divided by 2. (if shift.quirk, Vx = Vx SHR 1)
                        if(quirks & QUIRKS_SHIFT) {
                            yArgument = xArgument;
                        }
                        uint8_t result = registers[yArgument] / 2;
                        storeALUResult(xArgument, result, registers[yArgument] & 0x10);
                        break;
                    }
                    case ALU_SHL: { // 8xyE - SHL Vx {, Vy} - Set Vx = Vx SHL 1.  If the most-significant bit of Vy is 1, then VF is set to 1, otherwise to 0. Then Vx is Vy multiplied by 2.   (if shift.quirk, Vx = Vx SHL 1)
                        if(quirks & QUIRKS_SHIFT) {
                            yArgument = xArgument;
                        }
                        uint8_t result = registers[yArgument] * 2;
                        storeALUResult(xArgument, result, registers[yArgument] & 0x80);
                        break;
                    }
                    default : {
                        fprintf(stderr, "%04X: unsupported 8xyN instruction %04X\n", pc, instructionWord);
                        stepResult = UNSUPPORTED_INSTRUCTION;
                        break;
                    }
                }
                break;
            }
            case INSN_SNE_REG: { // 9xy0 - SNE Vx, Vy - Skip next instruction if Vx != Vy.  The values of Vx and Vy are compared, and if they are not equal, the program counter is increased by 2.  
                if(imm4Argument != 0) {
                    fprintf(stderr, "%04X: unsupported 9XY0 instruction %04X\n", pc, instructionWord);
                    stepResult = UNSUPPORTED_INSTRUCTION;
                }
                if(registers[xArgument] != registers[yArgument]) {
                    nextPC = nextPC + getInstructionSize(memory, nextPC);
                }
                break;
            }
            case INSN_LD_I: { // Annn - LD I, addr - Set I = nnn.  
                I = imm12Argument;
                break;
            }
            case INSN_JP_V0: { // Bnnn - JP V0, addr - Jump to location nnn + V0.
                if(quirks & QUIRKS_JUMP) { // Ugh!
                    nextPC = (imm12Argument & 0xFF) + registers[xArgument] + (xArgument << 8);
                } else {
                    nextPC = imm12Argument + registers[0];
                }
                break;
            }
            case INSN_RND: { // Cxkk - RND Vx, byte - Set Vx = random byte AND kk.  The interpreter generates a random number from 0 to 255, which is then ANDed with the value kk. The results are stored in Vx. See instruction 8xy2 for more information on AND.
                registers[xArgument] = uniform_dist(e1) & imm8Argument;
                break;
            }
            case INSN_DRW: { // Dxyn - DRW Vx, Vy, nibble
                // Display n-byte sprite starting at memory location I at (Vx, Vy), set VF = collision.
                // The interpreter reads n bytes from memory, starting at the address stored in
                // I. These bytes are then displayed as sprites on screen at coordinates (Vx, Vy).
                // Sprites are XORed onto the existing screen. If this causes any pixels to be erased,
                // VF is set to 1, otherwise it is set to 0. If the sprite is positioned so part of it
                // is outside the coordinates of the display, it wraps around to the opposite side of
                // the screen. See instruction 8xy3 for more information on XOR, and section 2.4,
                // Display, for more information on the Chip-8 screen and sprites.
                registers[0xF] = 0;
                uint32_t screenWidth = extendedScreenMode ? 128 : 64;
                uint32_t screenHeight = extendedScreenMode ? 64 : 32;
                uint32_t pixelScale = extendedScreenMode ? 1 : 2;
                uint16_t spriteByteAddress = I;
                uint32_t byteCount = 1;
                uint32_t rowCount = imm4Argument;
                if(extendedScreenMode && (imm4Argument == 0)) {
                    // 16x16 sprite
                    rowCount = 16;
                    byteCount = 2;
                }
                for(int bitplane = 0; bitplane < 2; bitplane++) {
                    uint8_t planeMask = 1 << bitplane;
                    if(screenPlaneMask & planeMask) {
                        for(uint32_t rowIndex = 0; rowIndex < rowCount; rowIndex++) {
                            for(uint32_t byteIndex = 0; byteIndex < byteCount; byteIndex++) {
                                uint8_t byte = memory.read(spriteByteAddress++);
                                for(uint32_t bitIndex = 0; bitIndex < 8; bitIndex++) {
                                    bool hasPixel = (byte >> (7 - bitIndex)) & 0x1;
                                    uint32_t colIndex = bitIndex + byteIndex * 8;
                                    if(quirks & QUIRKS_CLIP) {
                                        hasPixel &= (((registers[xArgument] % screenWidth) + colIndex) < screenWidth) &&
                                            (((registers[yArgument] % screenHeight) + rowIndex) < screenHeight);
                                    }
                                    if(hasPixel) {
                                        uint32_t x = (registers[xArgument] + colIndex) % screenWidth;
                                        uint32_t y = (registers[yArgument] + rowIndex) % screenHeight;
                                        if(debug & DEBUG_DRAW) {
                                            printf("draw %d %d (%d)\n", x, y, x + y * 64);
                                        }
                                        for(uint32_t ygrid = 0; ygrid < pixelScale; ygrid++) {
                                            for(uint32_t xgrid = 0; xgrid < pixelScale; xgrid++) {
                                                int x2 = x * pixelScale + xgrid;
                                                int y2 = y * pixelScale + ygrid;
                                                registers[0xF] |= interface.draw(x2, y2, planeMask);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                break;
            }
            case INSN_SKP: {
                int opcode = instructionWord & 0xFF;
                switch(opcode) {
                    case SKP_KEY: { // Ex9E - SKP Vx - Skip next instruction if key with the value of Vx is pressed.  Checks the keyboard, and if the key corresponding to the value of Vx is currently in the down position, PC is increased by 2.
                        if(interface.pressed(registers[xArgument])) {
                            if(debug & DEBUG_KEYS) {
                                printf("clock %llu, pc %04X, SKP_KEY, key %d pressed\n", clock, pc, registers[xArgument]);
                            }
                            nextPC = nextPC + getInstructionSize(memory, nextPC);
                        }
                        break;
                    }
                    case SKNP_KEY: { // ExA1 - SKNP Vx - Skip next instruction if key with the value of Vx is not pressed.  Checks the keyboard, and if the key corresponding to the value of Vx is currently in the up position, PC is increased by 2.
                        if(!interface.pressed(registers[xArgument])) {
                            nextPC = nextPC + getInstructionSize(memory, nextPC);
                        } else {
                            if(debug & DEBUG_KEYS) {
                                printf("clock %llu, pc %04X, SKNP_KEY, key %d pressed\n", clock, pc, registers[xArgument]);
                            }
                        }
                        break;
                    }
                    default : {
                        fprintf(stderr, "%04X: unsupported ExNN instruction %04X\n", pc, instructionWord);
                        stepResult = UNSUPPORTED_INSTRUCTION;
                        break;
                    }
                }
                break;
            }
            case INSN_LD_SPECIAL :{
                int opcode = instructionWord & 0xFF;
                switch(opcode) {
                    case SPECIAL_GET_DELAY: { // Fx07 - LD Vx, DT - Set Vx = delay timer value.  The value of DT is placed into Vx.
                        registers[xArgument] = DT;
                        break;
                    }
                    case SPECIAL_KEYWAIT: { // Fx0A - LD Vx, K - Wait for a key press, store the value of the key in Vx.  All execution stops until a key is pressed, then the value of that key is stored in Vx.  
                        if(debug & DEBUG_KEYS) {
                            printf("waiting for key\n");
                        }
                        waitingForKeyPress = true;
                        keyDestinationRegister = xArgument;
                        break;
                    }
                    case SPECIAL_SET_DELAY: { // Fx15 - LD DT, Vx - Set delay timer = Vx.  DT is set equal to the value of Vx.

                        DT = registers[xArgument];
                        break;
                    }
                    case SPECIAL_SET_SOUND: { // Fx18 - LD ST, Vx - Set sound timer = Vx.  ST is set equal to the value of Vx.  
                        ST = registers[xArgument];
                        if(ST > 0) {
                            interface.startSound();
                        }
                        break;
                    }
                    case SPECIAL_ADD_INDEX: { // Fx1E - ADD I, Vx - Set I = I + Vx.  The values of I and Vx are added, and the results are stored in I.  
                        I += registers[xArgument];
                        break;
                    }
                    case SPECIAL_LD_DIGIT: { // Fx29 - LD F, Vx - Set I = location of sprite for digit Vx.  The value of I is set to the location for the hexadecimal sprite corresponding to the value of Vx. See section 2.4, Display, for more information on the Chip-8 hexadecimal font.  
                        I = memory.getDigitLocation(registers[xArgument]);
                        break;
                    }
                    case SPECIAL_LD_BIGDIGIT: { // FX30* - Point I to 10-byte font sprite for digit VX (0..9)
                        if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
                            I = memory.getBigDigitLocation(registers[xArgument]);
                        } else {
                            fprintf(stderr, "unsupported 0XXX instruction %04X (LD BIGF) - does this ROM require \"schip\" platform?\n", instructionWord);
                            stepResult = UNSUPPORTED_INSTRUCTION;
                        }
                        break;
                    }
                    case SPECIAL_LD_BCD: { // Fx33 - LD B, Vx - Store BCD representation of Vx in memory locations I, I+1, and I+2.  The interpreter takes the decimal value of Vx, and places the hundreds digit in memory at location in I, the tens digit at location I+1, and the ones digit at location I+2.
                        memory.write(I + 0, registers[xArgument] / 100);
                        memory.write(I + 1, (registers[xArgument] % 100) / 10);
                        memory.write(I + 2, registers[xArgument] % 10);
                        break;
                    }
                    case SPECIAL_LD_IVX: { // Fx55 - LD [I], Vx - Store registers V0 through Vx in memory starting at location I.  The interpreter copies the values of registers V0 through Vx into memory, starting at the address in I.  
                        for(int i = 0; i <= xArgument; i++) {
                            memory.write(I + i, registers[i]);
                        }
                        if(!(quirks & QUIRKS_LOAD_STORE)) {
                            I = I + xArgument + 1;
                        }
                        break;
                    }
                    case SPECIAL_LD_VXI: { // Fx65 - LD Vx, [I] - Read registers V0 through Vx from memory starting at location I.  The interpreter reads values from memory starting at location I into registers V0 through Vx.
                        for(int i = 0; i <= xArgument; i++) {
                            registers[i] = memory.read(I + i);
                        }
                        if(!(quirks & QUIRKS_LOAD_STORE)) {
                            I = I + xArgument + 1;
                        }
                        break;
                    }
                    case SPECIAL_STORE_RPL: {
                        if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
                            for(int i = 0; i <= std::min((uint16_t)7, xArgument); i++) {
                                RPL[i] = registers[i];
                            }
                        } else {
                            fprintf(stderr, "unsupported FXNN instruction %04X (STORE_RPL) - does this ROM require \"schip\" platform?\n", instructionWord);
                            stepResult = UNSUPPORTED_INSTRUCTION;
                        }
                        break;
                    }
                    case SPECIAL_LD_RPL: {
                        fprintf(stderr, "unsupported FxNN instruction %04X (LD_RPL) ignored\n", instructionWord);
                        if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
                            for(int i = 0; i <= std::min((uint16_t)7, xArgument); i++) {
                                registers[i] = RPL[i];
                            }
                        } else {
                            fprintf(stderr, "unsupported FXNN instruction %04X (LD_RPL) - does this ROM require \"schip\" platform?\n", instructionWord);
                            stepResult = UNSUPPORTED_INSTRUCTION;
                        }
                        break;
                    }
                    case SPECIAL_LD_I_16BIT: { // F000 NNNN
                        if(platform == XOCHIP) {
                            I = readU16(memory, pc + 2);
                        } else {
                            fprintf(stderr, "unsupported 0XXX instruction %04X (LD I NNNN) - does this ROM require \"xochip\" platform?\n", instructionWord);
                            stepResult = UNSUPPORTED_INSTRUCTION;
                        }
                        break;
                    }
                    case SPECIAL_SET_PLANES: { // plane n (0xFN01) select zero or more drawing planes by bitmask (0 <= n <= 3).
                        if(platform == XOCHIP) {
                            screenPlaneMask = xArgument;
                        } else {
                            fprintf(stderr, "unsupported 0XXX instruction %04X (SET PLANES) - does this ROM require \"xochip\" platform?\n", instructionWord);
                            stepResult = UNSUPPORTED_INSTRUCTION;
                        }
                        break;
                    }
                    case SPECIAL_SET_AUDIO: { // audio (0xF002) store 16 bytes starting at i in the audio pattern buffer. 
                        if(platform == XOCHIP) {
                            // XXX TBD
                        } else {
                            fprintf(stderr, "unsupported 0XXX instruction %04X (SET AUDIO) - does this ROM require \"xochip\" platform?\n", instructionWord);
                            stepResult = UNSUPPORTED_INSTRUCTION;
                        }
                        break;
                    }
                    default : {
                        fprintf(stderr, "unsupported FxNN instruction %04X\n", instructionWord);
                        stepResult = UNSUPPORTED_INSTRUCTION;
                        break;
                    }
                }
                break;
            }
        }
        pc = nextPC;
        return stepResult;
    }
};

void disassemble(uint16_t pc, uint16_t instructionWord, uint16_t wordAfter)
{
    enum InstructionHighNybble
    {
        INSN_SYS = 0x0,
        INSN_JP = 0x1,
        INSN_CALL = 0x2,
        INSN_SE_IMM = 0x3,
        INSN_SNE_IMM = 0x4,
        INSN_HIGH5 = 0x5,
        INSN_LD_IMM = 0x6,
        INSN_ADD_IMM = 0x7,
        INSN_ALU = 0x8,
        INSN_SNE_REG = 0x9,
        INSN_LD_I = 0xA,
        INSN_JP_V0 = 0xB,
        INSN_RND = 0xC,
        INSN_DRW = 0xD,
        INSN_SKP = 0xE,
        INSN_LD_SPECIAL = 0xF,
    };

    enum Series5Opcode // 5XYN low nybble
    {
        HIGH5_SE_REG = 0x0,
        HIGH5_LD_I_VXVY = 0x2,
        HIGH5_LD_VXVY_I = 0x3,
    };

    enum SYSOpcode
    {
        SYS_CLS = 0x0E0,
        SYS_RET = 0x0EE,
        SYS_SCROLL_DOWN = 0x0C0,
        SYS_SCROLL_UP = 0x0D0,
        SYS_SCROLL_RIGHT_4 = 0xFB,
        SYS_SCROLL_LEFT_4 = 0xFC,
        SYS_EXIT = 0xFD,
        SYS_ORIGINAL_SCREEN = 0xFE,
        SYS_EXTENDED_SCREEN = 0xFF,
    };

    enum SPECIALOpcode
    {
        SPECIAL_GET_DELAY = 0x07,
        SPECIAL_KEYWAIT = 0x0A,
        SPECIAL_SET_DELAY = 0x15,
        SPECIAL_SET_SOUND = 0x18,
        SPECIAL_ADD_INDEX = 0x1E,
        SPECIAL_LD_DIGIT = 0x29,
        SPECIAL_LD_BCD = 0x33,
        SPECIAL_LD_IVX = 0x55,
        SPECIAL_LD_VXI = 0x65,
        SPECIAL_STORE_RPL = 0x75, // XXX ignored 
        SPECIAL_LD_RPL = 0x85, // XXX ignored 
        SPECIAL_LD_BIGDIGIT = 0x30,
        SPECIAL_LD_I_16BIT = 0x00,
        SPECIAL_SET_PLANES = 0x01,
        SPECIAL_SET_AUDIO = 0x02,
    };

    enum SKPOpcode {
        SKP_KEY = 0x9E,
        SKNP_KEY = 0xA1,
    };

    enum ALUOpcode {
        ALU_LD = 0x0,
        ALU_OR = 0x1,
        ALU_AND = 0x2,
        ALU_XOR = 0x3,
        ALU_ADD = 0x4,
        ALU_SUB = 0x5,
        ALU_SHR = 0x6,
        ALU_SUBN = 0x7,
        ALU_SHL = 0xE,
    };

    uint8_t imm8Argument = instructionWord & 0x00FF;
    uint8_t imm4Argument = instructionWord & 0x000F;
    uint16_t imm12Argument = instructionWord & 0x0FFF;
    uint16_t xArgument = (instructionWord & 0x0F00) >> 8;
    uint16_t yArgument = (instructionWord & 0x00F0) >> 4;
    int highNybble = instructionWord >> 12;

    switch(highNybble) {
        case INSN_SYS: {
            uint16_t sysOpcode = instructionWord & 0xFFF;
            switch(sysOpcode) {
                case SYS_CLS: { // 00E0 - CLS - Clear the display.
                    printf("%04X: (%04X) CLS\n", pc, instructionWord);
                    break;
                }
                case SYS_RET: { //  00EE - RET - Return from a subroutine.  The interpreter sets the program counter to the address at the top of the stack, then subtracts 1 from the stack pointer.
                    printf("%04X: (%04X) RET\n", pc, instructionWord);
                    break;
                }
                case SYS_SCROLL_RIGHT_4: { // 00FB*    Scroll display 4 pixels right
                    printf("%04X: (%04X) SCROLLRIGHT 4\n", pc, instructionWord);
                    break;
                }
                case SYS_SCROLL_LEFT_4: { // 00FC*    Scroll display 4 pixels left
                    printf("%04X: (%04X) SCROLLLEFT 4\n", pc, instructionWord);
                    break;
                }
                case SYS_EXIT: { // 00FD*    Exit CHIP interpreter
                    printf("%04X: (%04X) EXIT\n", pc, instructionWord);
                    break;
                }
                case SYS_EXTENDED_SCREEN: { // 00FF*    Enable extended screen mode for full-screen graphics
                    printf("%04X: (%04X) EXTENDEDSCREEN\n", pc, instructionWord);
                    break;
                }
                case SYS_ORIGINAL_SCREEN: { // 00FE*    Disable extended screen mode
                    printf("%04X: (%04X) ORIGINALSCREEN\n", pc, instructionWord);
                    break;
                }
                default : { // Opcode undefined or is a range
                    if((sysOpcode & 0xFF0) == SYS_SCROLL_UP) {
                        printf("%04X: (%04X) SCROLLUP %d\n", pc, instructionWord, imm4Argument);
                    } else if((sysOpcode & 0xFF0) == SYS_SCROLL_DOWN) {
                        printf("%04X: (%04X) SCROLLDN %d\n", pc, instructionWord, imm4Argument);
                    } else {
                        printf("%04X: (%04X) ???\n", pc, instructionWord);
                    }
                    break;
                }
            }
            break;
        }
        case INSN_JP: { // 1nnn - JP addr - Jump to location nnn.  The interpreter sets the program counter to nnn.
            printf("%04X: (%04X) JP %X\n", pc, instructionWord, imm12Argument);
            break;
        }
        case INSN_CALL: { // 2nnn - CALL addr - Call subroutine at nnn.  The interpreter increments the stack pointer, then puts the current PC on the top of the stack. The PC is then set to nnn.
            printf("%04X: (%04X) CALL %X\n", pc, instructionWord, imm12Argument);
            break;
        }
        case INSN_SE_IMM: { // 3xkk - SE Vx, byte - Skip next instruction if Vx = kk.  The interpreter compares register Vx to kk, and if they are equal, increments the program counter by 2.
            printf("%04X: (%04X) SE V%X %X\n", pc, instructionWord, xArgument, imm8Argument);
            break;
        }
        case INSN_SNE_IMM: { // 4xkk - SNE Vx, byte - Skip next instruction if Vx != kk.  The interpreter compares register Vx to kk, and if they are not equal, increments the program counter by 2.
            printf("%04X: (%04X) SNE V%X %X\n", pc, instructionWord, xArgument, imm8Argument);
            break;
        }
        case INSN_HIGH5: {
            uint8_t opcode = instructionWord & 0xF;
            switch(opcode) {
                case HIGH5_LD_I_VXVY : { // save vx - vy (0x5XY2) save an inclusive range of registers to memory starting at i.
                    printf("%04X: (%04X) LD I V%X %X\n", pc, instructionWord, xArgument, imm8Argument);
                    break;
                }
                case HIGH5_LD_VXVY_I : { // load vx - vy (0x5XY3) load an inclusive range of registers from memory starting at i.
                    printf("%04X: (%04X) LD V%X %X I\n", pc, instructionWord, xArgument, imm8Argument);
                    break;
                }
                case HIGH5_SE_REG : { // 5xy0 - SE Vx, Vy - Skip next instruction if Vx = Vy.  The interpreter compares register Vx to register Vy, and if they are equal, increments the program counter by 2.
                    printf("%04X: (%04X) SE V%X V%X\n", pc, instructionWord, xArgument, yArgument);
                    break;
                }
                default : {
                    printf("%04X: (%04X) ???\n", pc, instructionWord);
                    break;
                }
            }
            break;
        }
        case INSN_LD_IMM: { // 6xkk - LD Vx, byte - Set Vx = kk.  The interpreter puts the value kk into register Vx.  
            printf("%04X: (%04X) LD V%X %X\n", pc, instructionWord, xArgument, imm8Argument);
            break;
        }
        case INSN_ADD_IMM: { // 7xkk - ADD Vx, byte - Set Vx = Vx + kk.  Adds the value kk to the value of register Vx, then stores the result in Vx.
            printf("%04X: (%04X) ADD V%X, %X\n", pc, instructionWord, xArgument, imm8Argument);
            break;
        }
        case INSN_ALU: {
            int opcode = instructionWord & 0x000F;
            switch(opcode) {
                case ALU_LD: { // 8xy0 - LD Vx, Vy - Set Vx = Vy.  Stores the value of register Vy in register Vx.  
                    printf("%04X: (%04X) LD V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                    break;
                }
                case ALU_OR: { // 8xy1 - OR Vx, Vy - Set Vx = Vx OR Vy.
                    printf("%04X: (%04X) OR V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                    break;
                }
                case ALU_AND: { // 8xy2 - AND Vx, Vy - Set Vx = Vx AND Vy.
                    printf("%04X: (%04X) AND V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                    break;
                }
                case ALU_XOR: { // 8xy3 - XOR Vx, Vy -  Set Vx = Vx XOR Vy.
                    printf("%04X: (%04X) XOR V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                    break;
                }
                case ALU_ADD: { // 8xy4 - ADD Vx, Vy - Set Vx = Vx + Vy, set VF = carry.  The values of Vx and Vy are added together. If the result is greater than 8 bits (i.e., > 255,) VF is set to 1, otherwise 0. Only the lowest 8 bits of the result are kept, and stored in Vx.
                    printf("%04X: (%04X) ADD V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                    break;
                }
                case ALU_SUB: { // 8xy5 - SUB Vx, Vy - Set Vx = Vx - Vy, set VF = NOT borrow.  If Vx > Vy, then VF is set to 1, otherwise 0. Then Vy is subtracted from Vx, and the results stored in Vx.
                    printf("%04X: (%04X) SUB V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                    break;
                }
                case ALU_SUBN: { // 8xy7 - SUBN Vx, Vy - Set Vx = Vy - Vx, set VF = NOT borrow.  If Vy > Vx, then VF is set to 1, otherwise 0. Then Vx is subtracted from Vy, and the results stored in Vx.
                    printf("%04X: (%04X) SUBN V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                    break;
                }
                case ALU_SHR: { // 8xy6 - SHR Vx {, Vy} - Set Vx = Vy SHR 1.  If the least-significant bit of Vy is 1, then VF is set to 1, otherwise 0. Then Vx is Vy divided by 2. (if shift.quirk, Vx = Vx SHR 1)
                    printf("%04X: (%04X) SHR V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                    break;
                }
                case ALU_SHL: { // 8xyE - SHL Vx {, Vy} - Set Vx = Vx SHL 1.  If the most-significant bit of Vy is 1, then VF is set to 1, otherwise to 0. Then Vx is Vy multiplied by 2.   (if shift.quirk, Vx = Vx SHL 1)
                    printf("%04X: (%04X) SHL V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                    break;
                }
                default : {
                    printf("%04X: (%04X) ???\n", pc, instructionWord);
                    break;
                }
            }
            break;
        }
        case INSN_SNE_REG: { // 9xy0 - SNE Vx, Vy - Skip next instruction if Vx != Vy.  The values of Vx and Vy are compared, and if they are not equal, the program counter is increased by 2.  
            printf("%04X: (%04X) SNE V%X V%X\n", pc, instructionWord, xArgument, yArgument);
            break;
        }
        case INSN_LD_I: { // Annn - LD I, addr - Set I = nnn.  
            printf("%04X: (%04X) LD I %X\n", pc, instructionWord, imm12Argument);
            break;
        }
        case INSN_JP_V0: { // Bnnn - JP V0, addr - Jump to location nnn + V0.
            printf("%04X: (%04X) JP V0, %X\n", pc, instructionWord, imm12Argument);
            break;
        }
        case INSN_RND: { // Cxkk - RND Vx, byte - Set Vx = random byte AND kk.  The interpreter generates a random number from 0 to 255, which is then ANDed with the value kk. The results are stored in Vx. See instruction 8xy2 for more information on AND.
            printf("%04X: (%04X) RND V%X, %X\n", pc, instructionWord, xArgument, imm8Argument);
            break;
        }
        case INSN_DRW: { // Dxyn - DRW Vx, Vy, nibble
            printf("%04X: (%04X) DRW V%X, V%X, %X\n", pc, instructionWord, xArgument, yArgument, imm4Argument);
            break;
        }
        case INSN_SKP: {
            int opcode = instructionWord & 0xFF;
            switch(opcode) {
                case SKP_KEY: { // Ex9E - SKP Vx - Skip next instruction if key with the value of Vx is pressed.  Checks the keyboard, and if the key corresponding to the value of Vx is currently in the down position, PC is increased by 2.
                    printf("%04X: (%04X) SKP V%X\n", pc, instructionWord, xArgument);
                    break;
                }
                case SKNP_KEY: { // ExA1 - SKNP Vx - Skip next instruction if key with the value of Vx is not pressed.  Checks the keyboard, and if the key corresponding to the value of Vx is currently in the up position, PC is increased by 2.
                    printf("%04X: (%04X) SKNP V%X\n", pc, instructionWord, xArgument);
                    break;
                }
                default : {
                    printf("%04X: (%04X) ???\n", pc, instructionWord);
                    break;
                }
            }
            break;
        }
        case INSN_LD_SPECIAL :{
            int opcode = instructionWord & 0xFF;
            switch(opcode) {
                case SPECIAL_GET_DELAY: { // Fx07 - LD Vx, DT - Set Vx = delay timer value.  The value of DT is placed into Vx.
                    printf("%04X: (%04X) LD V%X, DT\n", pc, instructionWord, xArgument);
                    break;
                }
                case SPECIAL_KEYWAIT: { // Fx0A - LD Vx, K - Wait for a key press, store the value of the key in Vx.  All execution stops until a key is pressed, then the value of that key is stored in Vx.  
                    printf("%04X: (%04X) LD V%X, K\n", pc, instructionWord, xArgument);
                    break;
                }
                case SPECIAL_SET_DELAY: { // Fx15 - LD DT, Vx - Set delay timer = Vx.  DT is set equal to the value of Vx.
                    printf("%04X: (%04X) LD DT, V%X\n", pc, instructionWord, xArgument);
                    break;
                }
                case SPECIAL_SET_SOUND: { // Fx18 - LD ST, Vx - Set sound timer = Vx.  ST is set equal to the value of Vx.  
                    printf("%04X: (%04X) LD ST, V%X\n", pc, instructionWord, xArgument);
                    break;
                }
                case SPECIAL_ADD_INDEX: { // Fx1E - ADD I, Vx - Set I = I + Vx.  The values of I and Vx are added, and the results are stored in I.  
                    printf("%04X: (%04X) ADD I, V%X\n", pc, instructionWord, xArgument);
                    break;
                }
                case SPECIAL_LD_DIGIT: { // Fx29 - LD F, Vx - Set I = location of sprite for digit Vx.  The value of I is set to the location for the hexadecimal sprite corresponding to the value of Vx. See section 2.4, Display, for more information on the Chip-8 hexadecimal font.  
                    printf("%04X: (%04X) LD F, V%X\n", pc, instructionWord, xArgument);
                    break;
                }
                case SPECIAL_LD_BIGDIGIT: { // FX30* - Point I to 10-byte font sprite for digit VX (0..9)
                    printf("%04X: (%04X) LD BIGF, V%X\n", pc, instructionWord, xArgument);
                    break;
                }
                case SPECIAL_LD_BCD: { // Fx33 - LD B, Vx - Store BCD representation of Vx in memory locations I, I+1, and I+2.  The interpreter takes the decimal value of Vx, and places the hundreds digit in memory at location in I, the tens digit at location I+1, and the ones digit at location I+2.
                    printf("%04X: (%04X) LD B, V%X\n", pc, instructionWord, xArgument);
                    break;
                }
                case SPECIAL_LD_IVX: { // Fx55 - LD [I], Vx - Store registers V0 through Vx in memory starting at location I.  The interpreter copies the values of registers V0 through Vx into memory, starting at the address in I.  
                    printf("%04X: (%04X) LD [I], V%X\n", pc, instructionWord, xArgument);
                    break;
                }
                case SPECIAL_LD_VXI: { // Fx65 - LD Vx, [I] - Read registers V0 through Vx from memory starting at location I.  The interpreter reads values from memory starting at location I into registers V0 through Vx.
                    printf("%04X: (%04X) LD V%X, [I]\n", pc, instructionWord, xArgument);
                    break;
                }
                case SPECIAL_STORE_RPL: {
                    printf("%04X: (%04X) ???\n", pc, instructionWord);
                    break;
                }
                case SPECIAL_LD_RPL: {
                    printf("%04X: (%04X) ???\n", pc, instructionWord);
                    break;
                }
                case SPECIAL_LD_I_16BIT: { // F000 NNNN
                    printf("%04X: (%04X) LD I %04X\n", pc, instructionWord, wordAfter);
                    break;
                }
                case SPECIAL_SET_PLANES: { // plane n (0xFN01) select zero or more drawing planes by bitmask (0 <= n <= 3).
                    printf("%04X: (%04X) PLANES %04X\n", pc, instructionWord, xArgument);
                    break;
                }
                case SPECIAL_SET_AUDIO: { // audio (0xF002) store 16 bytes starting at i in the audio pattern buffer. 
                    printf("%04X: (%04X) AUDIO\n", pc, instructionWord);
                    break;
                }
                default : {
                    printf("%04X: (%04X) ???\n", pc, instructionWord);
                    break;
                }
            }
            break;
        }
    }
}


std::vector<uint8_t> digitSprites = {
#if 0
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80, // F
#else
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80, // F
#endif
};


std::vector<uint8_t> largeDigitSprites = {
#if 0
The MIT License (MIT)

Copyright (c) 2015, John Earnest

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
#endif
    0xFF, 0xFF, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xFF, 0xFF, // 0
    0x18, 0x78, 0x78, 0x18, 0x18, 0x18, 0x18, 0x18, 0xFF, 0xFF, // 1
    0xFF, 0xFF, 0x03, 0x03, 0xFF, 0xFF, 0xC0, 0xC0, 0xFF, 0xFF, // 2
    0xFF, 0xFF, 0x03, 0x03, 0xFF, 0xFF, 0x03, 0x03, 0xFF, 0xFF, // 3
    0xC3, 0xC3, 0xC3, 0xC3, 0xFF, 0xFF, 0x03, 0x03, 0x03, 0x03, // 4
    0xFF, 0xFF, 0xC0, 0xC0, 0xFF, 0xFF, 0x03, 0x03, 0xFF, 0xFF, // 5
    0xFF, 0xFF, 0xC0, 0xC0, 0xFF, 0xFF, 0xC3, 0xC3, 0xFF, 0xFF, // 6
    0xFF, 0xFF, 0x03, 0x03, 0x06, 0x0C, 0x18, 0x18, 0x18, 0x18, // 7
    0xFF, 0xFF, 0xC3, 0xC3, 0xFF, 0xFF, 0xC3, 0xC3, 0xFF, 0xFF, // 8
    0xFF, 0xFF, 0xC3, 0xC3, 0xFF, 0xFF, 0x03, 0x03, 0xFF, 0xFF, // 9
    0x7E, 0xFF, 0xC3, 0xC3, 0xC3, 0xFF, 0xFF, 0xC3, 0xC3, 0xC3, // A
    0xFC, 0xFC, 0xC3, 0xC3, 0xFC, 0xFC, 0xC3, 0xC3, 0xFC, 0xFC, // B
    0x3C, 0xFF, 0xC3, 0xC0, 0xC0, 0xC0, 0xC0, 0xC3, 0xFF, 0x3C, // C
    0xFC, 0xFE, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xFE, 0xFC, // D
    0xFF, 0xFF, 0xC0, 0xC0, 0xFF, 0xFF, 0xC0, 0xC0, 0xFF, 0xFF, // E
    0xFF, 0xFF, 0xC0, 0xC0, 0xFF, 0xFF, 0xC0, 0xC0, 0xC0, 0xC0  // F
};

struct Memory
{
    std::array<uint8_t, 65536> memory;

    std::array<uint16_t, 16> digitAddresses = {0};

    std::array<uint16_t, 16> largeDigitAddresses = {0};

    ChipPlatform platform;

    Memory(ChipPlatform platform) :
        platform(platform)
    {
        for(uint16_t i = 0; i < digitSprites.size(); i++) {
            uint16_t address = i;
            write(address, digitSprites[i]);
            if(i % 5 == 0) {
                digitAddresses[i / 5] = address;
            }
        }
        if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
            for(uint16_t i = 0; i < largeDigitSprites.size(); i++) {
                uint16_t address = (uint16_t)digitSprites.size() + i;
                write(address, largeDigitSprites[i]);
                if(i % 10 == 0) {
                    largeDigitAddresses[i / 10] = address;
                }
            }
        }
    }

    uint8_t read(uint16_t addr)
    {
        if(false)printf("read(%x) -> %x\n", addr, memory[addr]);
        if((platform != SCHIP_1_1) && (platform != XOCHIP)) {
            assert(addr < 4096);
        }
        return memory[addr];
    }

    void write(uint16_t addr, uint8_t v)
    {
        if((platform != SCHIP_1_1) && (platform != XOCHIP)) {
            assert(addr < 4096);
        }
        memory[addr] = v;
    }

    uint16_t getDigitLocation(uint8_t digit)
    {
        return digitAddresses[digit];
    }

    uint16_t getBigDigitLocation(uint8_t digit)
    {
        if((platform != SCHIP_1_1) && (platform != XOCHIP)) {
            abort();
        }
        if(platform != XOCHIP) {
            assert(digit < 10);
        }
        return largeDigitAddresses[digit];
    }
};

typedef std::array<uint8_t, 3> vec3ub;

vec3ub vec3ubFromInts(int r, int g, int b)
{
    return { (uint8_t)r, (uint8_t)g, (uint8_t)b };
}

struct Interface
{
    std::array<std::array<uint8_t, 128>, 64> display;
    std::array<vec3ub, 256> colorTable;
    bool displayChanged = true;
    bool closed = false;
    std::array<bool, 16> keyPressed;
    DisplayRotation rotation;

    bool succeeded = false;

    static int initialScaleFactor(DisplayRotation rotation) {
        switch(rotation) {
            case ROT_0: return 8;
            case ROT_90: return 4;
            case ROT_180: return 8;
            case ROT_270: return 4;
        }
    }

    mfb_window *window;
    int windowWidth;
    int windowHeight;
    uint32_t* windowBuffer;

    Interface(const std::string& name, DisplayRotation rotation) :
        rotation(rotation),
        windowWidth((((rotation == ROT_0) || (rotation == ROT_180)) ? 128 : 64) * initialScaleFactor(rotation)),
        windowHeight((((rotation == ROT_0) || (rotation == ROT_180)) ? 64 : 128) * initialScaleFactor(rotation))
    {
        keyPressed.fill(false);
        window = mfb_open_ex(name.c_str(), windowWidth, windowHeight, WF_RESIZABLE);
        if (window) {
            windowBuffer = new uint32_t[windowWidth * windowHeight];
            mfb_set_user_data(window, (void *) this);
            mfb_set_resize_callback(window, resizecb);
            mfb_set_keyboard_callback(window, keyboardcb);
            succeeded = true;
        }
        colorTable.fill({0,0,0});
        colorTable[0] = {0, 0, 0};
        colorTable[1] = {255, 255, 255};
        colorTable[2] = {170, 170, 170};
        colorTable[3] = {85, 85, 85};
        clear();
    }

    void scroll(int dx, int dy)
    {
        static std::array<std::array<uint8_t, 128>, 64> display2;
        display2 = display; // XXX do something better for production
        for(int y = 0; y < 64; y++) {
            int srcy = y + dy;
            for(int x = 0; x < 128; x++) {
                int srcx = x + dx;
                if((srcx >= 0) && (srcx < 128) && (srcy >= 0) && (srcy < 64)) {
                    display.at(y).at(x) = display2.at(srcy).at(srcx);
                } else {
                    display.at(y).at(x) = 0;
                }
            }
        }
    }

    bool redraw()
    {
        for(int row = 0; row < windowHeight; row++) {
            for(int col = 0; col < windowWidth; col++) {
                int displayX, displayY;
                switch(rotation) {
                    case ROT_0: {
                        displayX = col * 128 / windowWidth;
                        displayY = row * 64 / windowHeight;
                        break;
                    }
                    case ROT_90: {
                        displayX = row * 128 / windowHeight;
                        displayY = 64 - 1 - col * 64 / windowWidth;
                        break;
                    }
                    case ROT_180: {
                        displayX = 128 - 1 - col * 128 / windowWidth;
                        displayY = 64 - 1 - row * 64 / windowHeight;
                        break;
                    }
                    case ROT_270: {
                        displayX = 128 - 1 - row * 128 / windowHeight;
                        displayY = col * 64 / windowWidth;
                        break;
                    }
                }
                uint8_t pixel = display.at(displayY).at(displayX);
                auto &c = colorTable.at(pixel);
                windowBuffer[col + row * windowWidth] = MFB_RGB(c[0], c[1], c[2]);
            }
        }
        int status = mfb_update_ex(window, windowBuffer, windowWidth, windowHeight);
        closed = (status < 0);
        return status >= 0;
    }

    void resize(int width, int height)
    {
        windowWidth = width;
        windowHeight = height;
        delete[] windowBuffer;
        windowBuffer = new uint32_t[windowWidth * windowHeight];
    }

    static void resizecb(mfb_window *window, int width, int height)
    {
        Interface *ifc = static_cast<Interface *>(mfb_get_user_data(window));
        ifc->resize(width, height);
        // Optionally you can also change the viewport size
        mfb_set_viewport(window, 0, 0, width, height);
    }

    void keyboard(mfb_key key, mfb_key_mod mod, bool isPressed)
    {
        switch(key) {
            case KB_KEY_ESCAPE:
                if(isPressed) {
                    mfb_close(window);
                    closed = true;
                }
                break;
            case KB_KEY_1: keyPressed[0x1] = isPressed; break;
            case KB_KEY_2: keyPressed[0x2] = isPressed; break;
            case KB_KEY_3: keyPressed[0x3] = isPressed; break;
            case KB_KEY_4: keyPressed[0xC] = isPressed; break;
            case KB_KEY_Q: keyPressed[0x4] = isPressed; break;
            case KB_KEY_W: keyPressed[0x5] = isPressed; break;
            case KB_KEY_E: keyPressed[0x6] = isPressed; break;
            case KB_KEY_SPACE: keyPressed[0x6] = isPressed; break;
            case KB_KEY_R: keyPressed[0xD] = isPressed; break;
            case KB_KEY_A: keyPressed[0x7] = isPressed; break;
            case KB_KEY_S: keyPressed[0x8] = isPressed; break;
            case KB_KEY_D: keyPressed[0x9] = isPressed; break;
            case KB_KEY_F: keyPressed[0xE] = isPressed; break;
            case KB_KEY_Z: keyPressed[0xA] = isPressed; break;
            case KB_KEY_X: keyPressed[0x0] = isPressed; break;
            case KB_KEY_C: keyPressed[0xB] = isPressed; break;
            case KB_KEY_V: keyPressed[0xF] = isPressed; break;
            default: /* pass */ break;
        }
    }

    static void keyboardcb(mfb_window *window, mfb_key key, mfb_key_mod mod, bool isPressed)
    {
        Interface *ifc = static_cast<Interface *>(mfb_get_user_data(window));
        ifc->keyboard(key, mod, isPressed);
    }

    bool iterate()
    {
        bool success = true;
        if(displayChanged) {
            success = redraw();
            displayChanged = false;
        } else {
            success = (mfb_update_events(window) >= 0);
        }
        if(success) {
            mfb_wait_sync(window);
        }
        return success && !closed;
    }

    void startSound()
    {
        printf("sound\n");
    }

    void stopSound()
    {
    }

    bool pressed(uint8_t key)
    {
        return keyPressed[key];
    }

    bool draw(uint8_t x, uint8_t y, uint8_t planeMask)
    {
        bool erased = false;

        displayChanged = true; /* pixel will either be set or cleared... */

        if((x < 128) && (y < 64)) {
            auto& pixel = display.at(y).at(x);
            auto oldValue = pixel;
            for(int i = 0; i < 2; i++) {
                uint8_t bit = (0x1 << i);
                if(planeMask & bit) {
                    pixel = pixel ^ bit;
                }
            }
            if((oldValue != 0) && (pixel == 0)) {
                erased = true;
            }
        }

        return erased;
    }

    void clear()
    {
        for(auto& rowOfPixels : display) {
            for(auto& pixel : rowOfPixels) {
                pixel = 0;
            }
        }
    }
};

void usage(const char *name)
{
    fprintf(stderr, "usage: %s [options] ROM.o8\n", name);
    fprintf(stderr, "options:\n");
    fprintf(stderr, "\t--rate N           - issue N instructions per 60Hz field\n");
    fprintf(stderr, "\t--color N RRGGBB   - set color N to RRGGBB\n");
    fprintf(stderr, "\t--platform name    - enable platform, \"schip\" or \"xochip\"\n");
    fprintf(stderr, "\t--quirk name       - enable SCHIP quirk\n");
    fprintf(stderr, "\t                     \"jump\" : bits 11-8 of BNNN are also register number\n");
    fprintf(stderr, "\t                     \"shift\" : shift operates on Vx, not Vy\n");
    fprintf(stderr, "\t                     \"clip\" : sprites are not wrapped of sprites\n");
    fprintf(stderr, "\t                     \"loadstore\" : multi-register Vx load/store doesn't change I \n");
}

std::map<std::string, uint32_t> keywordsToQuirkValues = {
    {"shift", QUIRKS_SHIFT},
    {"loadstore", QUIRKS_LOAD_STORE},
    {"jump", QUIRKS_JUMP},
    {"clip", QUIRKS_CLIP},
    {"vforder", QUIRKS_VFORDER},
    {"logic", QUIRKS_LOGIC},
};

std::map<std::string, DisplayRotation> keywordsToRotationValues = {
    {"rot0", ROT_0},
    {"rot90", ROT_90},
    {"rot180", ROT_180},
    {"rot270", ROT_270},
};

int main(int argc, char **argv)
{
    const char *progname = argv[0];
    argc -= 1;
    argv += 1;

    int ticksPerField = 7;
    ChipPlatform platform = CHIP8;
    DisplayRotation rotation = ROT_0;
    uint32_t quirks = QUIRKS_NONE;
    std::map<int,vec3ub> colorTable;

    while((argc > 0) && (argv[0][0] == '-')) {
	if(strcmp(argv[0], "--color") == 0) {
            if(argc < 3) {
                fprintf(stderr, "--color option requires a color number and color.\n");
                usage(progname);
                exit(EXIT_FAILURE);
            }
            int colorIndex = atoi(argv[1]);
            uint32_t colorName = strtoul(argv[2], nullptr, 16);
            vec3ub color = vec3ubFromInts((colorName >> 16) & 0xff, (colorName >> 8) & 0xff, colorName & 0xff);
            colorTable[colorIndex] = color;
            argv += 3;
            argc -= 3;
        } else if(strcmp(argv[0], "--platform") == 0) {
            if(argc < 2) {
                fprintf(stderr, "--platform option requires a platform name.\n");
                usage(progname);
                exit(EXIT_FAILURE);
            }
            if(strcmp(argv[1], "schip") == 0) {
                platform = SCHIP_1_1;
            } else if(strcmp(argv[1], "xochip") == 0) {
                platform = XOCHIP;
            } else {
                fprintf(stderr, "unknown platform name \"%s\".\n", argv[1]);
                usage(progname);
                exit(EXIT_FAILURE);
            }
            argv += 2;
            argc -= 2;
        } else if(strcmp(argv[0], "--rotation") == 0) {
            if(argc < 2) {
                fprintf(stderr, "--rotation option requires a screen rotation value in degrees (0, 90, 180, 270).\n");
                usage(progname);
                exit(EXIT_FAILURE);
            }
            int angle = atoi(argv[1]);
            switch(angle) {
                case 0: rotation = ROT_0; break;
                case 90: rotation = ROT_90; break;
                case 180: rotation = ROT_180; break;
                case 270: rotation = ROT_270; break;
                default: {
                    fprintf(stderr, "rotation value %d is not implemented\n", angle);
                    usage(progname);
                    exit(EXIT_FAILURE);
                }
            }
            argv += 2;
            argc -= 2;
        } else if(strcmp(argv[0], "--quirk") == 0) {
            if(argc < 2) {
                fprintf(stderr, "--quirk option requires a quirk keyword.\n");
                usage(progname);
                exit(EXIT_FAILURE);
            }
            std::string quirkKeyword = argv[1];
            if(keywordsToQuirkValues.count(quirkKeyword) == 0) {
                fprintf(stderr, "unknown quirk keyword \"%s\".\n", argv[1]);
                usage(progname);
                exit(EXIT_FAILURE);
            }
            quirks |= keywordsToQuirkValues.at(quirkKeyword);
            argv += 2;
            argc -= 2;
        } else if(strcmp(argv[0], "--debug") == 0) {
            if(argc < 2) {
                fprintf(stderr, "--debug option requires a debug flag to enable.\n");
                usage(progname);
                exit(EXIT_FAILURE);
            }
            std::string debugKeyword = argv[1];
            if(keywordsToDebugFlags.count(debugKeyword) == 0) {
                fprintf(stderr, "unknown debug flag \"%s\".\n", argv[1]);
                usage(progname);
                exit(EXIT_FAILURE);
            }
            debug |= keywordsToDebugFlags.at(debugKeyword);
            fprintf(stderr, "debug value now 0x%02X\n", debug);
            argv += 2;
            argc -= 2;
        } else if(strcmp(argv[0], "--rate") == 0) {
            if(argc < 2) {
                fprintf(stderr, "--rate option requires a rate number value.\n");
                usage(progname);
                exit(EXIT_FAILURE);
            }
            ticksPerField = atoi(argv[1]);
            argv += 2;
            argc -= 2;
        } else if(
            (strcmp(argv[0], "-help") == 0) ||
            (strcmp(argv[0], "-h") == 0) ||
            (strcmp(argv[0], "-?") == 0))
        {
            usage(progname);
            exit(EXIT_SUCCESS);
	} else {
	    fprintf(stderr, "unknown parameter \"%s\"\n", argv[0]);
            usage(progname);
	    exit(EXIT_FAILURE);
	}
    }

    if(argc < 1) {
        usage(progname);
        exit(EXIT_FAILURE);
    }

#ifdef XCODE_MISSING_FILESYSTEM_FOR_YEARS
    char *base = strdup(argv[0]);
    Interface interface(basename(base), rotation);
    free(base);
#else
    std::filesystem::path base(argv[0]);
    Interface interface(base.filename().string(), rotation);
#endif

    Memory memory(platform);

    for(const auto& [index, color] : colorTable) {
        interface.colorTable[index] = color;
    }

    FILE *fp = fopen(argv[0], "rb");
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    for(uint16_t i = 0; i < size; i++) {
        uint8_t byte;
        fread(&byte, 1, 1, fp);
        memory.write(0x200 + i, byte);
    }
    fclose(fp);

    Chip8Interpreter<Memory,Interface> chip8(0x200, platform, quirks);

    std::chrono::time_point<std::chrono::system_clock> interfaceThen = std::chrono::system_clock::now();

    bool done = false;
    while(!done) {

        for(int i = 0; i < ticksPerField; i++) {
            Chip8Interpreter<Memory,Interface>::StepResult result = chip8.step(memory, interface);
            if((result == Chip8Interpreter<Memory,Interface>::UNSUPPORTED_INSTRUCTION) && (debug & DEBUG_FAIL_UNSUPPORTED_INSN)) {
                printf("exit on unsupported instruction\n");
                exit(EXIT_FAILURE);
            }
        }

        std::chrono::time_point<std::chrono::system_clock> interfaceNow;
        float dt;
        do { 
            interfaceNow = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(interfaceNow - interfaceThen);
            dt = elapsed.count();
        } while(dt < .0166f);

        done = !interface.iterate();
        chip8.tick(interface);
        interfaceThen = interfaceNow;
    }
}
