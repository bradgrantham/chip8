#include <array>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <sys/time.h>

// #include <MiniFB.h>

constexpr bool debug = false;

template <class MEMORY, class INTERFACE>
struct Chip8Interpreter
{
    std::array<uint8_t, 16> registers = {0};
    std::vector<uint16_t> stack;
    uint16_t I = 0;
    uint16_t pc = 0;
    uint16_t DT = 0;
    uint16_t ST = 0;

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

        int highNybble = instructionWord >> 12;

        if(debug) {
            printf("CHIP8: I:%04X pc:%04X DT:%02X ST:%02X\n", I, pc, DT, ST);
            for(int i = 0; i < 2; i++) {
                printf("    ");
                for(int j = 0; j < 8; j++) {
                    printf("V%X:%02X ", i * 8 + j, registers[i * 8 + j]);
                }
                puts("");
            }
        }

        switch(highNybble) {
            case INSN_SYS: {
                uint16_t sysOpcode = instructionWord & 0xFFF;
                if(debug)printf("%04X: (%04X) SYS%x\n", pc, instructionWord, sysOpcode);
                switch(sysOpcode) {
                    case SYS_CLS: { // 00E0 - CLS - Clear the display.
                        interface.clear();
                        break;
                    }
                    case SYS_RET: { //  00EE - RET - Return from a subroutine.  The interpreter sets the program counter to the address at the top of the stack, then subtracts 1 from the stack pointer.
                        pc = stack.back();
                        stack.pop_back();
                        break;
                    }
                }
                break;
            }
            case INSN_JP: { // 1nnn - JP addr - Jump to location nnn.  The interpreter sets the program counter to nnn.
                if(debug)printf("%04X: (%04X) JP %X\n", pc, instructionWord, imm12Argument);
                pc = imm12Argument;
                break;
            }
            case INSN_CALL: { // 2nnn - CALL addr - Call subroutine at nnn.  The interpreter increments the stack pointer, then puts the current PC on the top of the stack. The PC is then set to nnn.
                if(debug)printf("%04X: (%04X) CALL %X\n", pc, instructionWord, imm12Argument);
                stack.push_back(pc + 2);
                pc = imm12Argument;
                break;
            }
            case INSN_SE_IMM: { // 3xkk - SE Vx, byte - Skip next instruction if Vx = kk.  The interpreter compares register Vx to kk, and if they are equal, increments the program counter by 2.
                if(debug)printf("%04X: (%04X) SE V%X %X\n", pc, instructionWord, xArgument, imm12Argument);
                if(registers[xArgument] == imm8Argument) {
                    pc += 4;
                } else {
                    pc += 2;
                }
                break;
            }
            case INSN_SNE_IMM: { // 4xkk - SNE Vx, byte - Skip next instruction if Vx != kk.  The interpreter compares register Vx to kk, and if they are not equal, increments the program counter by 2.
                if(debug)printf("%04X: (%04X) SNE V%X %X\n", pc, instructionWord, xArgument, imm12Argument);
                if(registers[xArgument] != imm8Argument) {
                    pc += 4;
                } else {
                    pc += 2;
                }
                break;
            }
            case INSN_SE_REG: { // 5xy0 - SE Vx, Vy - Skip next instruction if Vx = Vy.  The interpreter compares register Vx to register Vy, and if they are equal, increments the program counter by 2.
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
                if(debug)printf("%04X: (%04X) ALU V%X, V%X\n", pc, instructionWord, xArgument, yArgument);
                switch(opcode) {
                    case ALU_LD: { // 8xy0 - LD Vx, Vy - Set Vx = Vy.  Stores the value of register Vy in register Vx.  
                        registers[xArgument] = registers[yArgument];
                        break;
                    }
                    case ALU_OR: { // 8xy1 - OR Vx, Vy - Set Vx = Vx OR Vy.
                        registers[xArgument] |= registers[yArgument];
                        break;
                    }
                    case ALU_AND: { // 8xy2 - AND Vx, Vy - Set Vx = Vx AND Vy.
                        registers[xArgument] &= registers[yArgument];
                        break;
                    }
                    case ALU_XOR: { // 8xy3 - XOR Vx, Vy -  Set Vx = Vx XOR Vy.
                        registers[xArgument] ^= registers[yArgument];
                        break;
                    }
                    case ALU_ADD: { // 8xy4 - ADD Vx, Vy - Set Vx = Vx + Vy, set VF = carry.  The values of Vx and Vy are added together. If the result is greater than 8 bits (i.e., > 255,) VF is set to 1, otherwise 0. Only the lowest 8 bits of the result are kept, and stored in Vx.
                        uint16_t result16 = registers[xArgument] + registers[yArgument];
                        registers[0xF] = (result16 > 255) ? 1 : 0;
                        registers[xArgument] = registers[xArgument] + registers[yArgument];
                        break;
                    }
                    case ALU_SUB: { // 8xy5 - SUB Vx, Vy - Set Vx = Vx - Vy, set VF = NOT borrow.  If Vx > Vy, then VF is set to 1, otherwise 0. Then Vy is subtracted from Vx, and the results stored in Vx.
                        registers[0xF] = (registers[xArgument] > registers[yArgument]) ? 1 : 0;
                        registers[xArgument] = registers[xArgument] - registers[yArgument];
                        break;
                    }
                    case ALU_SUBN: { // 8xy7 - SUBN Vx, Vy - Set Vx = Vy - Vx, set VF = NOT borrow.  If Vy > Vx, then VF is set to 1, otherwise 0. Then Vx is subtracted from Vy, and the results stored in Vx.
                        registers[0xF] = (registers[yArgument] > registers[xArgument]) ? 1 : 0;
                        registers[xArgument] = registers[yArgument] - registers[xArgument];
                        break;
                    }
                    case ALU_SHR: { // 8xy6 - SHR Vx {, Vy} - Set Vx = Vx SHR 1.  If the least-significant bit of Vx is 1, then VF is set to 1, otherwise 0. Then Vx is divided by 2.
                        registers[0xF] = (registers[xArgument] & 0x01) ? 1 : 0;
                        registers[xArgument] = registers[xArgument] / 2;
                        break;
                    }
                    case ALU_SHL: { // 8xyE - SHL Vx {, Vy} - Set Vx = Vx SHL 1.  If the most-significant bit of Vx is 1, then VF is set to 1, otherwise to 0. Then Vx is multiplied by 2.  
                        registers[0xF] = (registers[xArgument] & 0x80) ? 1 : 0;
                        registers[xArgument] = registers[xArgument] * 2;
                        break;
                    }
                }
                pc += 2;
                break;
            }
            case INSN_SNE_REG: { // 9xy0 - SNE Vx, Vy - Skip next instruction if Vx != Vy.  The values of Vx and Vy are compared, and if they are not equal, the program counter is increased by 2.  
                if(debug)printf("%04X: (%04X) SNE V%X V%X\n", pc, instructionWord, xArgument, yArgument);
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
                bool collision = 0;
                if(debug)printf("%04X: (%04X) DRW V%X, V%X, %X\n", pc, instructionWord, xArgument, yArgument, imm4Argument);
                for(int rowIndex = 0; rowIndex < imm4Argument; rowIndex++) {
                    uint8_t byte = memory.read(I + rowIndex);
                    for(int bitIndex = 0; bitIndex < 8; bitIndex++) {
                        collision |= interface.draw(registers[xArgument] + bitIndex, registers[yArgument] + rowIndex, (byte >> (7U - bitIndex)) & 0x1U);
                    }
                }
                registers[0xF] = collision ? 1 : 0;
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
                        registers[xArgument] = interface.waitKey();
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
                        break;
                    }
                    case SPECIAL_LD_VXI: { // Fx65 - LD Vx, [I] - Read registers V0 through Vx from memory starting at location I.  The interpreter reads values from memory starting at location I into registers V0 through Vx.
                        if(debug)printf("%04X: (%04X) LD V%X, [I]\n", pc, xArgument, instructionWord);
                        for(int i = 0; i <= xArgument; i++) {
                            registers[i] = memory.read(I + i);
                        }
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

struct Interface
{
    std::array<std::array<int, 64>, 32> display;

#if 0
    constexpr int windowScaleFactor = 4;
    mfb_window *window;
    int windowWidth = 64 * windowScaleFactor;
    int windowHeight = 32 * windowScaleFactor;
    uint32_t* windowBuffer;

    void redraw()
    {
    }

    void resize(int width, int height)
    {
        windowWidth = width;
        windowHeight = height;
        delete[] windowBuffer;
        windowBuffer = new uint32_t[64 * windowScaleFactor * 32 * windowScaleFactor];
        redraw();
    }

    static void resize(mfb_window *window, int width, int height)
    {
        Interface *ifc = static_case<Interface *>(mfb_get_user_data(window));
        ifc->resize(width, height);
        // Optionally you can also change the viewport size
        // mfb_set_viewport(window, 0, 0, width, height);
    }

    Interface()
    {
        window = mfb_open_ex("Noise Test", 64 * windowScaleFactor, 32 * windowScaleFactor, WF_RESIZABLE);
        windowBuffer = new uint32_t[64 * windowScaleFactor * 32 * windowScaleFactor];
        if (!window)
            return 0;
        mfb_set_user_data(window, (void *) this);
    }
#endif

    void startSound()
    {
        printf("beep\n");
    }

    void stopSound()
    {
    }

    uint8_t waitKey()
    {
        for(;;){}
    }

    bool pressed(uint8_t key)
    {
        return false;
    }

    bool draw(uint8_t x, uint8_t y, int value)
    {
        bool collision = display.at(y).at(x) ^ value;
        display.at(y).at(x) ^= value;
        return collision;
    }

    void output()
    {
        puts("\033[H.--------------------------------------------------------------------------------------------------------------------------------.");
        for(int row = 0; row < 32; row++) {
            printf("|");
            for(int column = 0; column < 64; column++) {
                printf("%s", display.at(row).at(column) ? "█" : " ");
                printf("%s", display.at(row).at(column) ? "█" : " ");
            }
            puts("|");
        }
        puts("`--------------------------------------------------------------------------------------------------------------------------------'");
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

int main(int argc, char **argv)
{
    Memory memory;
    Interface interface;

    if(argc != 2) {
        fprintf(stderr, "usage: %s ROM.o8\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    FILE *fp = fopen(argv[1], "rb");
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
            memory.digitAddresses[i / 5] = i;
        }
    }

    Chip8Interpreter<Memory,Interface> chip8;

    interface.clear();
    
    struct timeval interfaceThen, interfaceNow;
    gettimeofday(&interfaceThen, nullptr);

    int instructionsPerSecond = 1000;

    printf("\033[2J");

    chip8.pc = 0x200;
    while(1) {
        struct timeval instructionThen, instructionNow;
        gettimeofday(&instructionThen, nullptr);
        chip8.step(memory, interface);
        float dt = 0.0;
        do {
            gettimeofday(&instructionNow, nullptr);
            dt = instructionNow.tv_sec + instructionNow.tv_usec / 1000000.0 - (instructionThen.tv_sec + instructionThen.tv_usec / 1000000.0);
        } while(dt < 1.0 / instructionsPerSecond);

        gettimeofday(&interfaceNow, nullptr);
        dt = interfaceNow.tv_sec + interfaceNow.tv_usec / 1000000.0 - (interfaceThen.tv_sec + interfaceThen.tv_usec / 1000000.0);
        if(dt > .0166f) {
            interface.output();
            puts("");
            puts("");
            chip8.tick(interface);
            interfaceThen = interfaceNow;
        }
    }
}
