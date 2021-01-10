#include <array>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <sys/time.h>

#if 0


CLI args to set interface colors --color {0,1,2,3} RRGGBB (matches Octo JSON #RRGGBB, what about names?)
CLI to set platform --platform {chip8,schip,xochip}
CLI to set quirks --quirk {shift,loadstore,jump,clip}

SuperChip implies certain quirks, but turn those on from command line
    command-line flags - --mode turns on quirks, can turn on and off with args after that too
    That way platform turns on new functionality and quirks sets incompatible behavior
make memory be 64K
All skip instructions
    // The conditional skip instructions 0xEN9E (if -key then), 0xENA1 (if key then), 0x3XNN (if vx == NN then), 0x4XNN (if vx != NN then), 0x5XY0 (if vx == vy then) and 0x9XY0 (if vx != NN) will skip over this double-wide instruction, rather than skipping halfway through it.
    if(platform == XOCHIP) {
        uint8_t hiByte = memory.read(pc);
        uint8_t loByte = memory.read(pc + 1);
        uint16_t instructionWord = hiByte * 256 + loByte;
        if(instructionWord == 0xF000) {
            skip 4 bytes instead of 2
        }
    }
5XY2
    if(platform == XOCHIP) {
        save vx - vy (0x5XY2) save an inclusive range of registers to memory starting at i.
    } else {
        warn undefined
    }
5XY3
    if(platform == XOCHIP) {
        load vx - vy (0x5XY3) load an inclusive range of registers from memory starting at i.
    } else {
        warn undefined
    }
F000 NNNN
    if(platform == XOCHIP) {
        i := long NNNN (0xF000, 0xNNNN) load i with a 16-bit address.
    } else {
        warn undefined
    }
FN01
    if(platform == XOCHIP) {
        // plane n (0xFN01) select zero or more drawing planes by bitmask (0 <= n <= 3).
        interface->selectPlanes(mask);
    } else {
        warn undefined
    }
F002
    if(platform == XOCHIP) {
        audio (0xF002) store 16 bytes starting at i in the audio pattern buffer. 
        // !!!
    } else {
        warn undefined
    }
00DN
    if(platform == XOCHIP) {
        // scroll-up n (0x00DN) scroll the contents of the display up by 0-15 pixels.
        interface->scroll(0, -n)
    } else {
        warn undefined
    }
BNNN
    if(quirks & QUIRK_JUMP) {
        pc = (imm12Argument & 0xFF) + registers[0] + (xArgument << 8);
    }
00CN*    Scroll display N lines down
    if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
        interface->scroll(0, n)
    } else {
        warn undefined
    }
00FB*    Scroll display 4 pixels right
    if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
        interface->scroll(4, 0)
    } else {
        warn undefined
    }
00FC*    Scroll display 4 pixels left
    if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
        interface->scroll(-4, 0)
    } else {
        warn undefined
    }
00FD*    Exit CHIP interpreter
    if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
        return EXIT from step()
    } else {
        warn undefined
    }
00FE*    Disable extended screen mode
    if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
        note internally and draw 2x - default
    } else {
        warn undefined
    }
00FF*    Enable extended screen mode for full-screen graphics
    if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
        note internally and draw 1x
    } else {
        warn undefined
    }
DXYN*    Show N-byte sprite from M(I) at coords (VX,VY), VF := collision. If N=0 and extended mode, show 16x16 sprite.
    if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
        handle case of N=0 and extended mode
    }
    if(quirks & QUIRK_CLIP and extended mode) {
        set VF to number of collided rows
        dont draw wrapped, add clipped bottom rows to VF
    }
FX29: not used in SCHIP 1.1
FX30*    Point I to 10-byte font sprite for digit VX (0..9)
    if((platform == SCHIP_1_1) || (platform == XOCHIP)) {
        like digitAddresses but separate for 10-high
    } else {
        warn undefined
    }
FX75*    Store V0..VX in RPL user flags (X <= 7)
    ignore, warn undefined
FX85*    Read V0..VX from RPL user flags (X <= 7)
    ignore, warn undefined
FX55/FX65 do not increase I
    if(!(quirks & QUIRK_LOAD_STORE)) { add to I }
8XY6, 8XYE shift and test VX, not VY
    if(quirks & QUIRK_SHIFT) { use xArgument instead }

#endif


#include <MiniFB.h>

constexpr bool debug = false;

constexpr uint32_t QUIRKS_NONE = 0x01;           /* shift VX instead of VY */
constexpr uint32_t QUIRKS_SHIFT = 0x01;           /* shift VX instead of VY */
constexpr uint32_t QUIRKS_LOAD_STORE = 0x02;      /* don't add X + 1 to I */
constexpr uint32_t QUIRKS_JUMP = 0x04;            /* VX is used as offset *and* X used as address high nybble */
constexpr uint32_t QUIRKS_CLIP = 0x08;            /* no draw or collide wrapped, VX += rows off bottom */

enum ChipPlatform
{
    CHIP8,
    SCHIP_1_1,
    XOCHIP
};

template <class MEMORY, class INTERFACE>
struct Chip8Interpreter
{
    ChipPlatform platform;
    uint32_t quirks;

    std::array<uint8_t, 16> registers = {0};
    std::vector<uint16_t> stack;
    uint16_t I = 0;
    uint16_t pc = 0;
    uint16_t DT = 0;
    uint16_t ST = 0;
    bool extendedScreenMode = false;
    uint32_t screenPlaneMask = 0x1;

    bool waitingForKeyPress = false;
    bool waitingForKeyRelease = false;
    uint8_t keyPressed;
    uint8_t keyDestinationRegister;

    Chip8Interpreter(uint16_t initialPC, ChipPlatform platform, uint32_t quirks) :
        platform(platform),
        quirks(quirks),
        pc(initialPC)
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
        INSN_SE_REG = 0x5,
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

    enum SYSOpcode
    {
        SYS_CLS = 0x0E0,
        SYS_RET = 0x0EE,
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

    void step(MEMORY& memory, INTERFACE& interface)
    {
        uint8_t hiByte = memory.read(pc);
        uint8_t loByte = memory.read(pc + 1);
        uint16_t instructionWord = hiByte * 256 + loByte;
        uint8_t imm8Argument = instructionWord & 0x00FF;
        uint8_t imm4Argument = instructionWord & 0x000F;
        uint16_t imm12Argument = instructionWord & 0x0FFF;
        uint16_t xArgument = (instructionWord & 0x0F00) >> 8;
        uint16_t yArgument = (instructionWord & 0x00F0) >> 4;

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
                printf("pressed %d now wait for release\n", whichKey);
                keyPressed = whichKey;
                waitingForKeyPress = false;
                waitingForKeyRelease = true;
            } else {
                return;
            }
        }

        if(waitingForKeyRelease) {
            bool wasReleased = !interface.pressed(keyPressed);

            if(wasReleased) {
                printf("key wait over\n");
                waitingForKeyRelease = false;
                registers[keyDestinationRegister] = keyPressed;
            } else {
                return;
            }
        }

        int highNybble = instructionWord >> 12;

        if(debug) {
            printf("CHIP8: pc:%04X I:%04X DT:%02X ST:%02X ", pc, I, DT, ST);
            for(int i = 0; i < 16; i++) {
                printf("%02X ", registers[i]);
            }
            puts("");
        }

        switch(highNybble) {
            case INSN_SYS: {
                uint16_t sysOpcode = instructionWord & 0xFFF;
                switch(sysOpcode) {
                    case SYS_CLS: { // 00E0 - CLS - Clear the display.
                        if(debug)printf("%04X: (%04X) CLS\n", pc, instructionWord);
                        interface.clear();
                        break;
                    }
                    case SYS_RET: { //  00EE - RET - Return from a subroutine.  The interpreter sets the program counter to the address at the top of the stack, then subtracts 1 from the stack pointer.
                        if(debug)printf("%04X: (%04X) RET\n", pc, instructionWord);
                        pc = stack.back();
                        stack.pop_back();
                        break;
                    }
                    default : {
                        fprintf(stderr, "%04X: unsupported 0NNN instruction %04X ignored\n", pc, instructionWord);
                        break;
                    }
                }
                pc += 2;
                break;
            }
            case INSN_JP: { // 1nnn - JP addr - Jump to location nnn.  The interpreter sets the program counter to nnn.
                if(debug)printf("%04X: (%04X) JP %X\n", pc, instructionWord, imm12Argument);
                pc = imm12Argument;
                break;
            }
            case INSN_CALL: { // 2nnn - CALL addr - Call subroutine at nnn.  The interpreter increments the stack pointer, then puts the current PC on the top of the stack. The PC is then set to nnn.
                if(debug)printf("%04X: (%04X) CALL %X\n", pc, instructionWord, imm12Argument);
                stack.push_back(pc);
                pc = imm12Argument;
                break;
            }
            case INSN_SE_IMM: { // 3xkk - SE Vx, byte - Skip next instruction if Vx = kk.  The interpreter compares register Vx to kk, and if they are equal, increments the program counter by 2.
                if(debug)printf("%04X: (%04X) SE V%X %X\n", pc, instructionWord, xArgument, imm8Argument);
                if(registers[xArgument] == imm8Argument) {
                    pc += 4;
                } else {
                    pc += 2;
                }
                break;
            }
            case INSN_SNE_IMM: { // 4xkk - SNE Vx, byte - Skip next instruction if Vx != kk.  The interpreter compares register Vx to kk, and if they are not equal, increments the program counter by 2.
                if(debug)printf("%04X: (%04X) SNE V%X %X\n", pc, instructionWord, xArgument, imm8Argument);
                if(registers[xArgument] != imm8Argument) {
                    pc += 4;
                } else {
                    pc += 2;
                }
                break;
            }
            case INSN_SE_REG: { // 5xy0 - SE Vx, Vy - Skip next instruction if Vx = Vy.  The interpreter compares register Vx to register Vy, and if they are equal, increments the program counter by 2.
                if(imm4Argument != 0) {
                    fprintf(stderr, "%04X: unsupported instruction %04X\n", pc, instructionWord);
                    abort();
                }
                if(debug)printf("%04X: (%04X) SE V%X V%X\n", pc, instructionWord, xArgument, yArgument);
                if(registers[xArgument] == registers[yArgument]) {
                    pc += 4;
                } else {
                    pc += 2;
                }
                break;
            }
            case INSN_LD_IMM: { // 6xkk - LD Vx, byte - Set Vx = kk.  The interpreter puts the value kk into register Vx.  
                if(debug)printf("%04X: (%04X) LD V%X %X\n", pc, instructionWord, xArgument, imm8Argument);
                registers[xArgument] = imm8Argument;
                pc += 2;
                break;
            }
            case INSN_ADD_IMM: { // 7xkk - ADD Vx, byte - Set Vx = Vx + kk.  Adds the value kk to the value of register Vx, then stores the result in Vx.
                if(debug)printf("%04X: (%04X) ADD V%X, %X\n", pc, instructionWord, xArgument, imm8Argument);
                registers[xArgument] = registers[xArgument] + imm8Argument;
                pc += 2;
                break;
            }
            case INSN_ALU: {
                int opcode = instructionWord & 0x000F;
                switch(opcode) {
                    case ALU_LD: { // 8xy0 - LD Vx, Vy - Set Vx = Vy.  Stores the value of register Vy in register Vx.  
                        if(debug)printf("%04X: (%04X) LD V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                        registers[xArgument] = registers[yArgument];
                        break;
                    }
                    case ALU_OR: { // 8xy1 - OR Vx, Vy - Set Vx = Vx OR Vy.
                        if(debug)printf("%04X: (%04X) OR V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                        registers[xArgument] |= registers[yArgument];
                        break;
                    }
                    case ALU_AND: { // 8xy2 - AND Vx, Vy - Set Vx = Vx AND Vy.
                        if(debug)printf("%04X: (%04X) AND V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                        registers[xArgument] &= registers[yArgument];
                        break;
                    }
                    case ALU_XOR: { // 8xy3 - XOR Vx, Vy -  Set Vx = Vx XOR Vy.
                        if(debug)printf("%04X: (%04X) XOR V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                        registers[xArgument] ^= registers[yArgument];
                        break;
                    }
                    case ALU_ADD: { // 8xy4 - ADD Vx, Vy - Set Vx = Vx + Vy, set VF = carry.  The values of Vx and Vy are added together. If the result is greater than 8 bits (i.e., > 255,) VF is set to 1, otherwise 0. Only the lowest 8 bits of the result are kept, and stored in Vx.
                        if(debug)printf("%04X: (%04X) ADD V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                        uint16_t result16 = registers[xArgument] + registers[yArgument];
                        registers[0xF] = (result16 > 255) ? 1 : 0;
                        registers[xArgument] = registers[xArgument] + registers[yArgument];
                        break;
                    }
                    case ALU_SUB: { // 8xy5 - SUB Vx, Vy - Set Vx = Vx - Vy, set VF = NOT borrow.  If Vx > Vy, then VF is set to 1, otherwise 0. Then Vy is subtracted from Vx, and the results stored in Vx.
                        if(debug)printf("%04X: (%04X) SUB V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                        registers[0xF] = (registers[xArgument] >= registers[yArgument]) ? 1 : 0;
                        registers[xArgument] = registers[xArgument] - registers[yArgument];
                        break;
                    }
                    case ALU_SUBN: { // 8xy7 - SUBN Vx, Vy - Set Vx = Vy - Vx, set VF = NOT borrow.  If Vy > Vx, then VF is set to 1, otherwise 0. Then Vx is subtracted from Vy, and the results stored in Vx.
                        if(debug)printf("%04X: (%04X) SUBN V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                        registers[0xF] = (registers[yArgument] >= registers[xArgument]) ? 1 : 0;
                        registers[xArgument] = registers[yArgument] - registers[xArgument];
                        break;
                    }
                    case ALU_SHR: { // 8xy6 - SHR Vx {, Vy} - Set Vx = Vy SHR 1.  If the least-significant bit of Vy is 1, then VF is set to 1, otherwise 0. Then Vx is Vy divided by 2. (if shift.quirk, Vx = Vx SHR 1)
                        if(debug)printf("%04X: (%04X) SHR V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                        registers[0xF] = registers[yArgument] & 0x1;
                        registers[xArgument] = registers[yArgument] / 2;
                        break;
                    }
                    case ALU_SHL: { // 8xyE - SHL Vx {, Vy} - Set Vx = Vx SHL 1.  If the most-significant bit of Vy is 1, then VF is set to 1, otherwise to 0. Then Vx is Vy multiplied by 2.   (if shift.quirk, Vx = Vx SHL 1)
                        if(debug)printf("%04X: (%04X) SHL V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                        registers[0xF] = (registers[yArgument] >> 7) & 0x1;
                        registers[xArgument] = registers[yArgument] * 2;
                        break;
                    }
                    default : {
                        fprintf(stderr, "%04X: unsupported 8xyN instruction %04X\n", pc, instructionWord);
                        abort();
                        break;
                    }
                }
                pc += 2;
                break;
            }
            case INSN_SNE_REG: { // 9xy0 - SNE Vx, Vy - Skip next instruction if Vx != Vy.  The values of Vx and Vy are compared, and if they are not equal, the program counter is increased by 2.  
                if(debug)printf("%04X: (%04X) SNE V%X V%X\n", pc, instructionWord, xArgument, yArgument);
                if(imm4Argument != 0) {
                    fprintf(stderr, "%04X: unsupported 9XY0 instruction %04X\n", pc, instructionWord);
                    abort();
                }
                if(registers[xArgument] != registers[yArgument]) {
                    pc += 4;
                } else {
                    pc += 2;
                }
                break;
            }
            case INSN_LD_I: { // Annn - LD I, addr - Set I = nnn.  
                if(debug)printf("%04X: (%04X) LD I %X\n", pc, instructionWord, imm12Argument);
                I = imm12Argument;
                pc += 2;
                break;
            }
            case INSN_JP_V0: { // Bnnn - JP V0, addr - Jump to location nnn + V0.
                if(debug)printf("%04X: (%04X) JP V0, %X\n", pc, instructionWord, imm12Argument);
                pc = imm12Argument + registers[0];
                break;
            }
            case INSN_RND: { // Cxkk - RND Vx, byte - Set Vx = random byte AND kk.  The interpreter generates a random number from 0 to 255, which is then ANDed with the value kk. The results are stored in Vx. See instruction 8xy2 for more information on AND.
                if(debug)printf("%04X: (%04X) RND V%X, %X\n", pc, instructionWord, xArgument, imm8Argument);
                registers[xArgument] = random() & 0xFF & imm8Argument;
                pc += 2;
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
                bool erased = 0;
                if(debug)printf("%04X: (%04X) DRW V%X, V%X, %X\n", pc, instructionWord, xArgument, yArgument, imm4Argument);
                for(int rowIndex = 0; rowIndex < imm4Argument; rowIndex++) {
                    uint8_t byte = memory.read(I + rowIndex);
                    for(int bitIndex = 0; bitIndex < 8; bitIndex++) {
                        int x = registers[xArgument] + bitIndex;
                        int y = registers[yArgument] + rowIndex;
                        x = x % 64;
                        y = y % 32;
                        bool oneErased = false;
                        for(int ygrid = 0; ygrid < 2; ygrid++) {
                            for(int xgrid = 0; xgrid < 2; xgrid++) {
                                oneErased = interface.draw(x * 2 + xgrid, y * 2 + ygrid, (byte >> (7U - bitIndex)) & 0x1U, 0x1);
                            }
                        }
                        erased |= oneErased;
                    }
                }
                registers[0xF] = erased ? 1 : 0;
                pc += 2;
                break;
            }
            case INSN_SKP: {
                int opcode = instructionWord & 0xFF;
                switch(opcode) {
                    case SKP_KEY: { // Ex9E - SKP Vx - Skip next instruction if key with the value of Vx is pressed.  Checks the keyboard, and if the key corresponding to the value of Vx is currently in the down position, PC is increased by 2.
                        if(debug)printf("%04X: (%04X) SKP V%X\n", pc, instructionWord, xArgument);
                        if(interface.pressed(xArgument)) {
                            pc += 2;
                        }
                        break;
                    }
                    case SKNP_KEY: { // ExA1 - SKNP Vx - Skip next instruction if key with the value of Vx is not pressed.  Checks the keyboard, and if the key corresponding to the value of Vx is currently in the up position, PC is increased by 2.
                        if(debug)printf("%04X: (%04X) SKNP V%X\n", pc, instructionWord, xArgument);
                        if(!interface.pressed(xArgument)) {
                            pc += 2;
                        }
                        break;
                    }
                    default : {
                        fprintf(stderr, "%04X: unsupported ExNN instruction %04X\n", pc, instructionWord);
                        abort();
                        break;
                    }
                }
                pc += 2;
                break;
            }
            case INSN_LD_SPECIAL :{
                int opcode = instructionWord & 0xFF;
                switch(opcode) {
                    case SPECIAL_GET_DELAY: { // Fx07 - LD Vx, DT - Set Vx = delay timer value.  The value of DT is placed into Vx.
                        if(debug)printf("%04X: (%04X) LD V%X, DT\n", pc, instructionWord, xArgument);
                        registers[xArgument] = DT;
                        break;
                    }
                    case SPECIAL_KEYWAIT: { // Fx0A - LD Vx, K - Wait for a key press, store the value of the key in Vx.  All execution stops until a key is pressed, then the value of that key is stored in Vx.  
                        if(debug)printf("%04X: (%04X) LD V%X, K\n", pc, instructionWord, xArgument);
                        printf("waiting for key\n");
                        waitingForKeyPress = true;
                        keyDestinationRegister = xArgument;
                        break;
                    }
                    case SPECIAL_SET_DELAY: { // Fx15 - LD DT, Vx - Set delay timer = Vx.  DT is set equal to the value of Vx.
                        if(debug)printf("%04X: (%04X) LD DT, V%X\n", pc, instructionWord, xArgument);

                        DT = registers[xArgument];
                        break;
                    }
                    case SPECIAL_SET_SOUND: { // Fx18 - LD ST, Vx - Set sound timer = Vx.  ST is set equal to the value of Vx.  
                        if(debug)printf("%04X: (%04X) LD ST, V%X\n", pc, instructionWord, xArgument);
                        ST = registers[xArgument];
                        if(ST > 0) {
                            interface.startSound();
                        }
                        break;
                    }
                    case SPECIAL_ADD_INDEX: { // Fx1E - ADD I, Vx - Set I = I + Vx.  The values of I and Vx are added, and the results are stored in I.  
                        if(debug)printf("%04X: (%04X) ADD I, V%X\n", pc, instructionWord, xArgument);
                        I += registers[xArgument];
                        break;
                    }
                    case SPECIAL_LD_DIGIT: { // Fx29 - LD F, Vx - Set I = location of sprite for digit Vx.  The value of I is set to the location for the hexadecimal sprite corresponding to the value of Vx. See section 2.4, Display, for more information on the Chip-8 hexadecimal font.  
                        if(debug)printf("%04X: (%04X) LD F, V%X\n", pc, instructionWord, xArgument);
                        I = memory.getDigitLocation(registers[xArgument]);
                        break;
                    }
                    case SPECIAL_LD_BCD: { // Fx33 - LD B, Vx - Store BCD representation of Vx in memory locations I, I+1, and I+2.  The interpreter takes the decimal value of Vx, and places the hundreds digit in memory at location in I, the tens digit at location I+1, and the ones digit at location I+2.
                        if(debug)printf("%04X: (%04X) LD B, V%X\n", pc, instructionWord, xArgument);
                        memory.write(I + 0, registers[xArgument] / 100);
                        memory.write(I + 1, (registers[xArgument] % 100) / 10);
                        memory.write(I + 2, registers[xArgument] % 10);
                        break;
                    }
                    case SPECIAL_LD_IVX: { // Fx55 - LD [I], Vx - Store registers V0 through Vx in memory starting at location I.  The interpreter copies the values of registers V0 through Vx into memory, starting at the address in I.  
                        if(debug)printf("%04X: (%04X) LD [I], V%X\n", pc, instructionWord, xArgument);
                        for(int i = 0; i <= xArgument; i++) {
                            memory.write(I + i, registers[i]);
                        }
                        I = I + xArgument + 1;
                        break;
                    }
                    case SPECIAL_LD_VXI: { // Fx65 - LD Vx, [I] - Read registers V0 through Vx from memory starting at location I.  The interpreter reads values from memory starting at location I into registers V0 through Vx.
                        if(debug)printf("%04X: (%04X) LD V%X, [I]\n", pc, instructionWord, xArgument);
                        for(int i = 0; i <= xArgument; i++) {
                            registers[i] = memory.read(I + i);
                        }
                        I = I + xArgument + 1;
                        break;
                    }
                    default : {
                        fprintf(stderr, "unsupported FxNN instruction %04X\n", instructionWord);
                        abort();
                        break;
                    }
                }
                pc += 2;
                break;
            }
        }
    }
};

struct Memory
{
    std::array<uint8_t, 4096> memory;

    std::array<uint16_t, 16> digitAddresses;

    uint8_t read(uint16_t addr)
    {
        if(false)printf("read(%x) -> %x\n", addr, memory[addr]);
        assert(addr < 4096);
        return memory[addr];
    }

    void write(uint16_t addr, uint8_t v)
    {
        assert(addr < 4096);
        memory[addr] = v;
    }

    uint16_t getDigitLocation(uint8_t digit)
    {
        return digitAddresses[digit];
    }
};

typedef std::array<uint8_t, 3> vec3ub;

struct Interface
{
    std::array<std::array<uint8_t, 128>, 64> display = {0};
    std::array<vec3ub, 256> colorTable;
    bool displayChanged = false;
    bool closed = false;
    std::array<bool, 16> keyPressed = {0};

    bool succeeded = false;

    static constexpr int windowInitialScaleFactor = 8;
    mfb_window *window;
    int windowWidth = 128 * windowInitialScaleFactor;
    int windowHeight = 64 * windowInitialScaleFactor;
    uint32_t* windowBuffer;

    bool redraw()
    {
        for(int row = 0; row < windowHeight; row++) {
            for(int col = 0; col < windowWidth; col++) {
                int displayX = col * 128 / windowWidth;
                int displayY = row * 64 / windowHeight;
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
        // mfb_set_viewport(window, 0, 0, width, height);
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
        // mfb_wait_sync(window);
        return success && !closed;
    }

    Interface()
    {
        window = mfb_open_ex("CHIP8", windowWidth, windowHeight, WF_RESIZABLE);
        if (window) {
            windowBuffer = new uint32_t[windowWidth * windowHeight];
            mfb_set_user_data(window, (void *) this);
            mfb_set_resize_callback(window, resizecb);
            mfb_set_keyboard_callback(window, keyboardcb);
            succeeded = true;
        }
        vec3ub black = {0,0,0};
        std::fill(colorTable.begin(), colorTable.end(), black);
        colorTable[0] = {0, 0, 0};
        colorTable[1] = {255, 255, 255};
        colorTable[2] = {170, 170, 170};
        colorTable[3] = {85, 85, 85};
    }

    void startSound()
    {
        printf("beep\n");
    }

    void stopSound()
    {
    }

    bool pressed(uint8_t key)
    {
        return keyPressed[key];
    }

    bool draw(uint8_t x, uint8_t y, bool newValue, uint8_t planeMask)
    {
        bool erased = false;
        if((x <= 128) && (y < 64)) {
            for(int i = 0; i < 2; i++) {
                uint8_t bit = (0x1 << i);
                if(planeMask & bit) {
                    bool displayValue = (display.at(y).at(x) & bit);
                    bool result = displayValue ^ newValue;
                    displayChanged |= (displayValue != result);
                    erased |= !result;
                    display.at(y).at(x) = (display.at(y).at(x) & ~bit) | (result ? bit : 0);
                }
            }
        }
        return erased;
    }

    void clear()
    {
        display = {0};
    }
};

std::vector<uint8_t> digitSprites = {
    0xF0,
    0x90,
    0x90,
    0x90,
    0xF0,
    
    0x20,
    0x60,
    0x20,
    0x20,
    0x70,
    
    0xF0,
    0x10,
    0xF0,
    0x80,
    0xF0,
    
    0xF0,
    0x10,
    0xF0,
    0x10,
    0xF0,
            
    0x90,
    0x90,
    0xF0,
    0x10,
    0x10,
    
    0xF0,
    0x80,
    0xF0,
    0x10,
    0xF0,
    
    0xF0,
    0x80,
    0xF0,
    0x90,
    0xF0,
    
    0xF0,
    0x10,
    0x20,
    0x40,
    0x40,
   
    0xF0,
    0x90,
    0xF0,
    0x90,
    0xF0,

    0xF0,
    0x90,
    0xF0,
    0x10,
    0xF0,
    
    0xF0,
    0x90,
    0xF0,
    0x90,
    0x90,
    
    0xE0,
    0x90,
    0xE0,
    0x90,
    0xE0,

    0xF0,
    0x80,
    0x80,
    0x80,
    0xF0,
    
    0xE0,
    0x90,
    0x90,
    0x90,
    0xE0,
    
    0xF0,
    0x80,
    0xF0,
    0x80,
    0xF0,
    
    0xF0,
    0x80,
    0xF0,
    0x80,
    0x80,
};

void usage(const char *name)
{
    fprintf(stderr, "usage: %s [options] ROM.o8\n", name);
    fprintf(stderr, "options:\n");
    fprintf(stderr, "\t-rate N      # issue N instructions per 60Hz field\n");
}

int main(int argc, char **argv)
{
    const char *progname = argv[0];
    argc -= 1;
    argv += 1;

    Memory memory;
    Interface interface;
    int ticksPerField = 7;

    while((argc > 0) && (argv[0][0] == '-')) {
	if(strcmp(argv[0], "--rate") == 0) {
            if(argc < 2) {
                fprintf(stderr, "--rate option requires a rate number value.\n");
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

    FILE *fp = fopen(argv[0], "rb");
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    for(long i = 0; i < size; i++) {
        uint8_t byte;
        fread(&byte, 1, 1, fp);
        memory.write(0x200 + i, byte);
    }
    fclose(fp);

    for(size_t i = 0; i < digitSprites.size(); i++) {
        memory.write(0x200 - 80 + i, digitSprites[i]);
        if(i % 5 == 0) {
            memory.digitAddresses[i / 5] = 0x200 - 80 + i;
        }
    }

    Chip8Interpreter<Memory,Interface> chip8(0x200, /* ChipPlatform:: */ CHIP8, QUIRKS_NONE);

    struct timeval interfaceThen, interfaceNow;
    gettimeofday(&interfaceThen, nullptr);

    bool done = false;
    while(!done) {

        for(int i = 0; i < ticksPerField; i++) {
            chip8.step(memory, interface);
        }

        float dt;
        do { 
            gettimeofday(&interfaceNow, nullptr);
            dt = interfaceNow.tv_sec + interfaceNow.tv_usec / 1000000.0 - (interfaceThen.tv_sec + interfaceThen.tv_usec / 1000000.0);
        } while(dt < .0166f);

        done = !interface.iterate();
        chip8.tick(interface);
        interfaceThen = interfaceNow;
    }
}
