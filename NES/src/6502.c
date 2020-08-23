#include "6502.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct
{
	char code[3]; // OP code of the instruction

	// Executes the instruction, changing the state of the cpu, some instructions care if a page boundary was crossed
	// to fetch its data. returns any additional cycles required to complete the instruction if any 
	uint32_t(*operation)(State6502* cpu, bool c);

	// Fetches data and loads it into cpu->operand for the instruction. Returns true if a page was crossed
	bool(*adressing_mode)(State6502* cpu);

	uint32_t cycles; // Number of cycles required for instruction to complete
} Instruction;

static Instruction opcodes[256];

void clock(State6502* cpu)
{
	static uint32_t remaining = 0;

	if (remaining == 0)
	{
		uint8_t opcode = bus_read(cpu->bus, cpu->PC++);
		Instruction inst = opcodes[opcode];

		remaining = inst.cycles;
		remaining--;

		bool b = inst.adressing_mode(cpu);
		remaining += inst.operation(cpu, b);
	}
	else
		remaining--;
}

void reset(State6502* cpu)
{
	cpu->SP -= 3;
	cpu->status.flags.I = 1;

	bus_write(cpu->bus, 0x4015, 0); // All channels disabled
}

void power_on(State6502* cpu)
{
	cpu->status.reg = 0x34; // Interrupt disabled
	cpu->A = 0;
	cpu->X = 0;
	cpu->Y = 0;
	cpu->SP = 0xFD;

	bus_write(cpu->bus, 0x4015, 0); // All channels disabled
	bus_write(cpu->bus, 0x4017, 0); // Frame IRQ disabled

	for (uint16_t addr = 0x4000; addr <= 0x400F; addr++)
	{
		bus_write(cpu->bus, addr, 0);
	}

	for (uint16_t addr = 0x4010; addr <= 0x4013; addr++)
	{
		bus_write(cpu->bus, addr, 0);
	}
}

// 13 Adressing modes

// Accumilator addressing
bool ACC(State6502* cpu)
{
	return false;
}

// Immediate addressing
bool IMM(State6502* cpu)
{
	cpu->operand = bus_read(cpu->bus, cpu->PC++);
	return false;
}

// Zero page
bool ZP0(State6502* cpu)
{
	cpu->addr = bus_read(cpu->bus, cpu->PC++);
	cpu->operand = bus_read(cpu->bus, cpu->addr);

	return false;
}

// Zero page with X index
bool ZPX(State6502* cpu)
{
	uint16_t addr_low = bus_read(cpu->bus, cpu->PC++);
	cpu->addr = (addr_low + cpu->X) & 0x00FF;
	cpu->operand = bus_read(cpu->bus, cpu->addr);
	return false;
}

// Zero page with Y index
bool ZPY(State6502* cpu)
{
	uint16_t addr_low = bus_read(cpu->bus, cpu->PC++);
	cpu->addr = (addr_low + cpu->Y) & 0x00FF;
	cpu->operand = bus_read(cpu->bus, cpu->addr);
	return false;
}

// Absolulte
bool ABS(State6502* cpu)
{
	uint16_t addr_low = bus_read(cpu->bus, cpu->PC++);
	uint16_t addr_high = bus_read(cpu->bus, cpu->PC++);
	cpu->addr = (addr_high << 8) | addr_low;
	cpu->operand = bus_read(cpu->bus, cpu->addr);
	return false;
}

// Absolute with X index
bool ABX(State6502* cpu)
{
	uint16_t addr_low = bus_read(cpu->bus, cpu->PC++);
	uint16_t addr_high = bus_read(cpu->bus, cpu->PC++);
	uint16_t addr = (addr_high << 8) | addr_low;
	cpu->addr = addr + cpu->X;
	cpu->operand = bus_read(cpu->bus, cpu->addr);

	return (addr & 0xFF00) != (cpu->addr & 0xFF00);
}

// Absolulte with Y index
bool ABY(State6502* cpu)
{
	uint16_t addr_low = bus_read(cpu->bus, cpu->PC++);
	uint16_t addr_high = bus_read(cpu->bus, cpu->PC++);
	uint16_t addr = (addr_high << 8) | addr_low;
	cpu->addr = addr + cpu->Y;
	cpu->operand = bus_read(cpu->bus, cpu->addr);

	return (addr & 0xFF00) != (cpu->addr & 0xFF00);
}

// Implied
bool IMP(State6502* cpu)
{
	return false;
}

// Relative 
bool REL(State6502* cpu)
{
	cpu->operand = bus_read(cpu->bus, cpu->PC++);
	return false;
}

// Indirect
bool IND(State6502* cpu)
{
	uint16_t addr_low = bus_read(cpu->bus, cpu->PC++);
	uint16_t addr_high = bus_read(cpu->bus, cpu->PC++);
	uint16_t addr = (addr_high << 8) | addr_low;

	// Read high bits
	cpu->indirect_fetch = bus_read(cpu->bus, addr + 1);
	cpu->indirect_fetch <<= 8;
	cpu->indirect_fetch |= bus_read(cpu->bus, addr);
	
	/* Hack: only the JMP instruction uses indirect addressing, however it could also be using absolute addressing
	 * If absolute addressing was used JMP would set the program counter to the value of cpu->operand, however
	 * if indirect addressing was used JMP would set the program counter to the value of cpu->indirect_fetch
	 * The problem is that JMP has no way of knowing which addressing mode was used, this is where the hack comes in,
	 * both indirect and absolute addressing will never cross a page boundary, so they should both return false. However 
	 * we set the indirect addressing function to return true so the JMP instruction can tell what addressing mode was used
	 * this has no effect on other instructions as JMP is the only one using indirect addressing
	 */
	return true;
}

// Indirect with X index
bool IZX(State6502* cpu)
{
	uint16_t zp_addr = bus_read(cpu->bus, cpu->PC++);
	zp_addr = (zp_addr + cpu->X) & 0x00FF;

	uint16_t addr_low = bus_read(cpu->bus, zp_addr);
	uint16_t addr_high = bus_read(cpu->bus, (zp_addr + 1) & 0x00FF);
	cpu->addr = (addr_high << 8) | addr_low;
	cpu->operand = bus_read(cpu->bus, cpu->addr);

	return false;
}

// Indirect with Y index
bool IZY(State6502* cpu)
{
	uint16_t zp_addr = bus_read(cpu->bus, cpu->PC++);
	uint16_t addr_low = bus_read(cpu->bus, zp_addr);
	uint16_t addr_high = bus_read(cpu->bus, (zp_addr + 1) & 0x00FF); // Read the next zero page address

	uint16_t addr = ((addr_high << 8) | addr_low);
	cpu->addr = addr + cpu->Y;
	cpu->operand = bus_read(cpu->bus, cpu->addr);
	return (addr & 0xFF00) != (cpu->addr & 0xFF00);
}

// 56 instructions

// Add with carry
uint32_t ADC(State6502* cpu, bool c)
{
	uint16_t result = (uint16_t)cpu->A + (uint16_t)cpu->operand + (uint16_t)cpu->status.flags.C;

	// Set status flags
	cpu->status.flags.C = result > 255;
	cpu->status.flags.Z = (result & 0x00FF) == 0;
	cpu->status.flags.N = (result & 0x0080) == 0x0080;
	cpu->status.flags.V = ((~((uint16_t)cpu->A ^ (uint16_t)cpu->operand) & ((uint16_t)cpu->A ^ result)) & 0x0080) == 0x0080;

	// Store result into register A
	cpu->A = result & 0x00FF;

	return c ? 1 : 0;
}

// Logical AND
uint32_t AND(State6502* cpu, bool c)
{
	cpu->A = cpu->A & cpu->operand;

	// Set status flags
	cpu->status.flags.Z = cpu->A == 0;
	cpu->status.flags.N = (cpu->A & 0x0080) == 0x0080;

	return c ? 1 : 0;
}

// Arithmetic Shift Left TODO: FIXME
uint32_t ASL(State6502* cpu, bool c)
{
	uint16_t result = (uint16_t)cpu->A << 1;

	cpu->status.flags.C = (result & 0x0100) == 0x0100;
	cpu->status.flags.Z = (result & 0x00FF) == 0;
	cpu->status.flags.N = (result & 0x0080) == 0x0080;

	return 0;
}

// Branch if Carry Clear
uint32_t BCC(State6502* cpu, bool c)
{
	if (cpu->status.flags.C == 0)
	{
		cpu->PC += (int8_t)cpu->operand;

		return c ? 2 : 1;
	}

	return 0;
}

// Branch if Carry Set
uint32_t BCS(State6502* cpu, bool c)
{
	if (cpu->status.flags.C == 1)
	{
		cpu->PC += (int8_t)cpu->operand;

		return c ? 2 : 1;
	}

	return 0;
}

// Branch if Equal (zero flag set)
uint32_t BEQ(State6502* cpu, bool c)
{
	if (cpu->status.flags.Z == 1)
	{
		cpu->PC += (int8_t)cpu->operand;

		return c ? 2 : 1;
	}

	return 0;
}

// Bit Test
uint32_t BIT(State6502* cpu, bool c)
{
	uint8_t result = cpu->A & cpu->operand;

	cpu->status.flags.Z = result == 0;
	cpu->status.flags.V = (cpu->operand & (1 << 6)) == (1 << 6);
	cpu->status.flags.N = (cpu->operand & (1 << 7)) == (1 << 7);

	return 0;
}

// Branch if Minus (negative flag set)
uint32_t BMI(State6502* cpu, bool c)
{
	if (cpu->status.flags.N == 1)
	{
		cpu->PC += (int8_t)cpu->operand;

		return c ? 2 : 1;
	}

	return 0;
}

// Branch if not Equal (zero flag clear)
uint32_t BNE(State6502* cpu, bool c)
{
	if (cpu->status.flags.Z == 0)
	{
		cpu->PC += (int8_t)cpu->operand;

		return c ? 2 : 1;
	}

	return 0;
}

// Branch if Positive (negative flag clear)
uint32_t BPL(State6502* cpu, bool c)
{
	if (cpu->status.flags.N == 0)
	{
		cpu->PC += (int8_t)cpu->operand;

		return c ? 2 : 1;
	}

	return 0;
}

// Force Interrupt
uint32_t BRK(State6502* cpu, bool c)
{
	// This is a bug in the 6502, the address after the BRK instruction is not pushed to the stack
	// that address is skipped and the second address after the BRK instruction is pushed instead
	cpu->PC++;

	// Push PC to stack
	bus_write(cpu->bus, (uint16_t)0x0100 | (uint16_t)cpu->SP, (uint8_t)(cpu->PC >> 8)); // High byte
	cpu->SP--;

	bus_write(cpu->bus, (uint16_t)0x0100 | (uint16_t)cpu->SP, (uint8_t)(cpu->PC & 0x00FF)); // Low byte
	cpu->SP--;

	// Push status to stack, with bits 4 and 5 set
	bus_write(cpu->bus, (uint16_t)0x0100 | (uint16_t)cpu->SP, cpu->status.reg | 1 << 4 | 1 << 5);
	cpu->SP--;

	// Set PC to BRK vector 0xFFFE/F
	cpu->PC = bus_read(cpu->bus, 0xFFFF); // High byte
	cpu->PC = cpu->PC << 8;
	cpu->PC |= bus_read(cpu->bus, 0xFFFE); // Low byte

	// Set I flag
	cpu->status.flags.I = 1;

	return 0;
}

// Branch if Overflow Clear
uint32_t BVC(State6502* cpu, bool c)
{
	if (cpu->status.flags.V == 0)
	{
		cpu->PC += (int8_t)cpu->operand;

		return c ? 2 : 1;
	}

	return 0;
}

// Branch if Overflow Set
uint32_t BVS(State6502* cpu, bool c)
{
	if (cpu->status.flags.V == 1)
	{
		cpu->PC += (int8_t)cpu->operand;

		return c ? 2 : 1;
	}

	return 0;
}

// Clear Carry Flag
uint32_t CLC(State6502* cpu, bool c)
{
	cpu->status.flags.C = 0;
	return 0;
}

// Clear Decimal Mode
uint32_t CLD(State6502* cpu, bool c)
{
	cpu->status.flags.D = 0;
	return 0;
}

// Clear Interrupt Disable
uint32_t CLI(State6502* cpu, bool c)
{
	cpu->status.flags.I = 0;
	return 0;
}

// Clear Overflow Flag
uint32_t CLV(State6502* cpu, bool c)
{
	cpu->status.flags.V = 0;
	return 0;
}

// Compare A register
uint32_t CMP(State6502* cpu, bool c)
{
	cpu->status.flags.C = (cpu->A >= cpu->operand);
	cpu->status.flags.Z = (cpu->A == cpu->operand);

	uint16_t result = (uint16_t)cpu->A - (uint16_t)cpu->operand;
	cpu->status.flags.N = (result & 0x0080) == 0x0080;

	return c ? 1 : 0;
}

// Compare X Register
uint32_t CPX(State6502* cpu, bool c)
{
	cpu->status.flags.C = (cpu->X >= cpu->operand);
	cpu->status.flags.Z = (cpu->X == cpu->operand);

	uint16_t result = (uint16_t)cpu->X - (uint16_t)cpu->operand;
	cpu->status.flags.N = (result & 0x0080) == 0x0080;

	return 0;
}

// Compare Y Register
uint32_t CPY(State6502* cpu, bool c)
{
	cpu->status.flags.C = (cpu->Y >= cpu->operand);
	cpu->status.flags.Z = (cpu->Y == cpu->operand);

	uint16_t result = (uint16_t)cpu->Y - (uint16_t)cpu->operand;
	cpu->status.flags.N = (result & 0x0080) == 0x0080;

	return 0;
}

// Decrement Memory
uint32_t DEC(State6502* cpu, bool c)
{
	uint8_t result = cpu->operand - 1;
	bus_write(cpu->bus, cpu->addr, result);

	cpu->status.flags.Z = result == 0;
	cpu->status.flags.N = (result & 0x0080) == 0x0080;
	return 0;
}

// Decrement X Register
uint32_t DEX(State6502* cpu, bool c)
{
	cpu->X--;

	cpu->status.flags.Z = cpu->X == 0;
	cpu->status.flags.N = (cpu->X & 0x0080) == 0x0080;
	return 0;
}

// Decrement Y Register
uint32_t DEY(State6502* cpu, bool c)
{
	cpu->Y--;

	cpu->status.flags.Z = cpu->Y == 0;
	cpu->status.flags.N = (cpu->Y & 0x0080) == 0x0080;
	return 0;
}

// Exclusive OR
uint32_t EOR(State6502* cpu, bool c)
{
	cpu->A = cpu->A ^ cpu->operand;

	cpu->status.flags.Z = cpu->A == 0;
	cpu->status.flags.N = (cpu->A & 0x0080) == 0x0080;

	return c ? 1 : 0;
}

// Increment Memory
uint32_t INC(State6502* cpu, bool c)
{
	uint8_t result = cpu->operand + 1;
	bus_write(cpu->bus, cpu->addr, result);

	cpu->status.flags.Z = result == 0;
	cpu->status.flags.N = (result & 0x0080) == 0x0080;
	return 0;
}

// Increment X Register
uint32_t INX(State6502* cpu, bool c)
{
	cpu->X++;

	cpu->status.flags.Z = cpu->X == 0;
	cpu->status.flags.N = (cpu->X & 0x0080) == 0x0080;
	return 0;
}

// Increment Y Register
uint32_t INY(State6502* cpu, bool c)
{
	cpu->Y++;

	cpu->status.flags.Z = cpu->Y == 0;
	cpu->status.flags.N = (cpu->Y & 0x0080) == 0x0080;
	return 0;
}

// Jump 
uint32_t JMP(State6502* cpu, bool c)
{
	// Indirect addressing used
	if (c)
	{
		cpu->PC = cpu->indirect_fetch;
	}
	else // Absolute addressing used
	{
		cpu->PC = (uint16_t)cpu->operand;
	}

	return 0;
}

// Jump to Subroutine
uint32_t JSR(State6502* cpu, bool c)
{
	// Set PC to the last byte of the JSR instruction
	cpu->PC--;

	// Push PC to stack
	bus_write(cpu->bus, (uint16_t)0x0100 | (uint16_t)cpu->SP, (uint8_t)(cpu->PC >> 8)); // High byte
	cpu->SP--;

	bus_write(cpu->bus, (uint16_t)0x0100 | (uint16_t)cpu->SP, (uint8_t)(cpu->PC & 0x00FF)); // Low byte
	cpu->SP--;

	cpu->PC = cpu->addr;

	return 0;
}

// Load Accumulator
uint32_t LDA(State6502* cpu, bool c)
{
	cpu->A = cpu->operand;

	cpu->status.flags.Z = cpu->A == 0;
	cpu->status.flags.N = (cpu->A & 0x0080) == 0x0080;

	return c ? 1 : 0;
}

// Load X Register
uint32_t LDX(State6502* cpu, bool c)
{
	cpu->X = cpu->operand;

	cpu->status.flags.Z = cpu->X == 0;
	cpu->status.flags.N = (cpu->X & 0x0080) == 0x0080;

	return c ? 1 : 0;
}

// Load Y Register
uint32_t LDY(State6502* cpu, bool c)
{
	cpu->Y = cpu->operand;

	cpu->status.flags.Z = cpu->Y == 0;
	cpu->status.flags.N = (cpu->Y & 0x0080) == 0x0080;

	return c ? 1 : 0;
}

// Logical Shift Right
uint32_t LSR(State6502* cpu, bool c)
{
	// TODO
}


// No Operation
uint32_t NOP(State6502* cpu, bool c)
{
	return 0;
}

// Logical Inclusive OR
uint32_t ORA(State6502* cpu, bool c)
{
	cpu->A = cpu->A | cpu->operand;

	cpu->status.flags.Z = cpu->A == 0;
	cpu->status.flags.N = (cpu->A & 0x0080) == 0x0080;

	return c ? 1 : 0;
}

// Push Accumulator
uint32_t PHA(State6502* cpu, bool c)
{
	bus_write(cpu->bus, (uint16_t)0x0100 | (uint16_t)cpu->SP, cpu->A);
	cpu->SP--;

	return 0;
}

// Push Processor Status
uint32_t PHP(State6502* cpu, bool c)
{
	// Unused bits 4 and 5 are set in a PHP instruction
	bus_write(cpu->bus, (uint16_t)0x0100 | (uint16_t)cpu->SP, cpu->status.reg | 1 << 4 | 1 << 5);
	cpu->SP--;

	return 0;
}

// Pull Accumulator
uint32_t PLA(State6502* cpu, bool c)
{
	cpu->SP++;
	cpu->A = bus_read(cpu->bus, (uint16_t)0x0100 | (uint16_t)cpu->SP);

	cpu->status.flags.Z = cpu->A == 0;
	cpu->status.flags.N = (cpu->A & 0x0080) == 0x0080;

	return 0;
}

// Pull Processor Status
uint32_t PLP(State6502* cpu, bool c)
{
	cpu->SP++;
	cpu->status.reg = bus_read(cpu->bus, (uint16_t)0x0100 | (uint16_t)cpu->SP);

	return 0;
}

// Rotate Left
uint32_t ROL(State6502* cpu, bool c)
{
	// TODO
}

// Rotate Right
uint32_t ROR(State6502* cpu, bool c)
{
	// TODO
}

// Return from Interrupt
uint32_t RTI(State6502* cpu, bool c)
{
	// Pull status from stack
	cpu->SP++;
	cpu->status.reg = bus_read(cpu->bus, (uint16_t)0x0100 | (uint16_t)cpu->SP);

	// Pull PC from stack
	cpu->SP++;
	uint16_t PCL = bus_read(cpu->bus, (uint16_t)0x0100 | (uint16_t)cpu->SP);
	cpu->SP++;
	uint16_t PCH = bus_read(cpu->bus, (uint16_t)0x0100 | (uint16_t)cpu->SP);

	cpu->PC = (PCH << 8) | PCL + 1;

	return 0;
}

// Return from Subroutine
uint32_t RTS(State6502* cpu, bool c)
{
	// Pull PC from stack
	cpu->SP++;
	uint16_t PCL = bus_read(cpu->bus, (uint16_t)0x0100 | (uint16_t)cpu->SP);
	cpu->SP++;
	uint16_t PCH = bus_read(cpu->bus, (uint16_t)0x0100 | (uint16_t)cpu->SP);

	cpu->PC = (PCH << 8) | PCL + 1;

	return 0;
}

// Subtract with Carry
uint32_t SBC(State6502* cpu, bool c)
{
	uint16_t value = ((uint16_t)cpu->operand) ^ 0x00FF;

	uint16_t temp = (uint16_t)cpu->A + value + (uint16_t)cpu->status.flags.C;
	cpu->status.flags.C = (temp & 0xFF00) != 0;
	cpu->status.flags.Z = (temp & 0x00FF) == 0;
	cpu->status.flags.V = (temp ^ (uint16_t)cpu->A) & (temp ^ value) & 0x0080;
	cpu->status.flags.N = (temp & 0x0080) == 0x0080;
	cpu->A = temp & 0x00FF;
	return c ? 1 : 0;
}

// Set Carry Flag
uint32_t SEC(State6502* cpu, bool c)
{
	cpu->status.flags.C = 1;
	return 0;
}

// Set Decimal Flag
uint32_t SED(State6502* cpu, bool c)
{
	cpu->status.flags.D = 1;
	return 0;
}

// Set Interrupt Disable
uint32_t SEI(State6502* cpu, bool c)
{
	cpu->status.flags.I = 1;
	return 0;
}

// Store Accumulator
uint32_t STA(State6502* cpu, bool c)
{
	bus_write(cpu->bus, cpu->addr, cpu->A);
	return 0;
}

// Store X Register
uint32_t STX(State6502* cpu, bool c)
{
	bus_write(cpu->bus, cpu->addr, cpu->X);
	return 0;
}

// Store Y Register
uint32_t STY(State6502* cpu, bool c)
{
	bus_write(cpu->bus, cpu->addr, cpu->Y);
	return 0;
}

// Transfer Accumulator to X
uint32_t TAX(State6502* cpu, bool c)
{
	cpu->X = cpu->A;

	cpu->status.flags.Z = cpu->X == 0;
	cpu->status.flags.N = (cpu->X & 0x0080) == 0x0080;
	return 0;
}

// Transfer Accumulator to Y
uint32_t TAY(State6502* cpu, bool c)
{
	cpu->Y = cpu->A;

	cpu->status.flags.Z = cpu->Y == 0;
	cpu->status.flags.N = (cpu->Y & 0x0080) == 0x0080;
	return 0;
}

// Transfer Stack Pointer to X
uint32_t TSX(State6502* cpu, bool c)
{
	cpu->X = cpu->SP;

	cpu->status.flags.Z = cpu->X == 0;
	cpu->status.flags.N = (cpu->X & 0x0080) == 0x0080;
	return 0;
}

// Transfer X to Accumulator
uint32_t TXA(State6502* cpu, bool c)
{
	cpu->A = cpu->X;

	cpu->status.flags.Z = cpu->A == 0;
	cpu->status.flags.N = (cpu->A & 0x0080) == 0x0080;
	return 0;
}

// Transfer X to Stack Pointer
uint32_t TXS(State6502* cpu, bool c)
{
	cpu->SP = cpu->X;

	cpu->status.flags.Z = cpu->SP == 0;
	cpu->status.flags.N = (cpu->SP & 0x0080) == 0x0080;
	return 0;
}

// Transfer Y to Accumulator
uint32_t TYA(State6502* cpu, bool c)
{
	cpu->A = cpu->Y;

	cpu->status.flags.Z = cpu->A == 0;
	cpu->status.flags.N = (cpu->A & 0x0080) == 0x0080;
	return 0;
}

uint32_t XXX(State6502* cpu, bool c)
{
	printf("[ERROR] Illegal Opcode Used");
	return 0;
}
static Instruction opcodes[256] = {
	{"BRK",BRK,IMM,7}, {"ORA",ORA,IZX,6}, {"???",XXX,IMP,2}, {"???",XXX,IMP,8}, {"???",NOP,IMP,3}, {"ORA",ORA,ZP0,3}, {"ASL",ASL,ZP0,5}, {"???",XXX,IMP,5}, {"PHP",PHP,IMP,3}, {"ORA",ORA,IMM,2}, {"ASL",ASL,IMP,2}, {"???",XXX,IMP,2}, {"???",NOP,IMP,4}, {"ORA",ORA,ABS,4}, {"ASL",ASL,ABS,6}, {"???",XXX,IMP,6},
	{"BPL",BPL,REL,2}, {"ORA",ORA,IZY,5}, {"???",XXX,IMP,2}, {"???",XXX,IMP,8}, {"???",NOP,IMP,4}, {"ORA",ORA,ZPX,4}, {"ASL",ASL,ZPX,6}, {"???",XXX,IMP,6}, {"CLC",CLC,IMP,2}, {"ORA",ORA,ABY,4}, {"???",NOP,IMP,2}, {"???",XXX,IMP,7}, {"???",NOP,IMP,4}, {"ORA",ORA,ABX,4}, {"ASL",ASL,ABX,7}, {"???",XXX,IMP,7},
	{"JSR",JSR,ABS,6}, {"AND",AND,IZX,6}, {"???",XXX,IMP,2}, {"???",XXX,IMP,8}, {"BIT",BIT,ZP0,3}, {"AND",AND,ZP0,3}, {"ROL",ROL,ZP0,5}, {"???",XXX,IMP,5}, {"PLP",PLP,IMP,4}, {"AND",AND,IMM,2}, {"ROL",ROL,IMP,2}, {"???",XXX,IMP,2}, {"BIT",BIT,ABS,4}, {"AND",AND,ABS,4}, {"ROL",ROL,ABS,6}, {"???",XXX,IMP,6},
	{"BMI",BMI,REL,2}, {"AND",AND,IZY,5}, {"???",XXX,IMP,2}, {"???",XXX,IMP,8}, {"???",NOP,IMP,4}, {"AND",AND,ZPX,4}, {"ROL",ROL,ZPX,6}, {"???",XXX,IMP,6}, {"SEC",SEC,IMP,2}, {"AND",AND,ABY,4}, {"???",NOP,IMP,2}, {"???",XXX,IMP,7}, {"???",NOP,IMP,4}, {"AND",AND,ABX,4}, {"ROL",ROL,ABX,7}, {"???",XXX,IMP,7},
	{"RTI",RTI,IMP,6}, {"EOR",EOR,IZX,6}, {"???",XXX,IMP,2}, {"???",XXX,IMP,8}, {"???",NOP,IMP,3}, {"EOR",EOR,ZP0,3}, {"LSR",LSR,ZP0,5}, {"???",XXX,IMP,5}, {"PHA",PHA,IMP,3}, {"EOR",EOR,IMM,2}, {"LSR",LSR,IMP,2}, {"???",XXX,IMP,2}, {"JMP",JMP,ABS,3}, {"EOR",EOR,ABS,4}, {"LSR",LSR,ABS,6}, {"???",XXX,IMP,6},
	{"BVC",BVC,REL,2}, {"EOR",EOR,IZY,5}, {"???",XXX,IMP,2}, {"???",XXX,IMP,8}, {"???",NOP,IMP,4}, {"EOR",EOR,ZPX,4}, {"LSR",LSR,ZPX,6}, {"???",XXX,IMP,6}, {"CLI",CLI,IMP,2}, {"EOR",EOR,ABY,4}, {"???",NOP,IMP,2}, {"???",XXX,IMP,7}, {"???",NOP,IMP,4}, {"EOR",EOR,ABX,4}, {"LSR",LSR,ABX,7}, {"???",XXX,IMP,7},
	{"RTS",RTS,IMP,6}, {"ADC",ADC,IZX,6}, {"???",XXX,IMP,2}, {"???",XXX,IMP,8}, {"???",NOP,IMP,3}, {"ADC",ADC,ZP0,3}, {"ROR",ROR,ZP0,5}, {"???",XXX,IMP,5}, {"PLA",PLA,IMP,4}, {"ADC",ADC,IMM,2}, {"ROR",ROR,IMP,2}, {"???",XXX,IMP,2}, {"JMP",JMP,IND,5}, {"ADC",ADC,ABS,4}, {"ROR",ROR,ABS,6}, {"???",XXX,IMP,6},
	{"BVS",BVS,REL,2}, {"ADC",ADC,IZY,5}, {"???",XXX,IMP,2}, {"???",XXX,IMP,8}, {"???",NOP,IMP,4}, {"ADC",ADC,ZPX,4}, {"ROR",ROR,ZPX,6}, {"???",XXX,IMP,6}, {"SEI",SEI,IMP,2}, {"ADC",ADC,ABY,4}, {"???",NOP,IMP,2}, {"???",XXX,IMP,7}, {"???",NOP,IMP,4}, {"ADC",ADC,ABX,4}, {"ROR",ROR,ABX,7}, {"???",XXX,IMP,7},
	{"???",NOP,IMP,2}, {"STA",STA,IZX,6}, {"???",NOP,IMP,2}, {"???",XXX,IMP,6}, {"STY",STY,ZP0,3}, {"STA",STA,ZP0,3}, {"STX",STX,ZP0,3}, {"???",XXX,IMP,3}, {"DEY",DEY,IMP,2}, {"???",NOP,IMP,2}, {"TXA",TXA,IMP,2}, {"???",XXX,IMP,2}, {"STY",STY,ABS,4}, {"STA",STA,ABS,4}, {"STX",STX,ABS,4}, {"???",XXX,IMP,4},
	{"BCC",BCC,REL,2}, {"STA",STA,IZY,6}, {"???",XXX,IMP,2}, {"???",XXX,IMP,6}, {"STY",STY,ZPX,4}, {"STA",STA,ZPX,4}, {"STX",STX,ZPY,4}, {"???",XXX,IMP,4}, {"TYA",TYA,IMP,2}, {"STA",STA,ABY,5}, {"TXS",TXS,IMP,2}, {"???",XXX,IMP,5}, {"???",NOP,IMP,5}, {"STA",STA,ABX,5}, {"???",XXX,IMP,5}, {"???",XXX,IMP,5},
	{"LDY",LDY,IMM,2}, {"LDA",LDA,IZX,6}, {"LDX",LDX,IMM,2}, {"???",XXX,IMP,6}, {"LDY",LDY,ZP0,3}, {"LDA",LDA,ZP0,3}, {"LDX",LDX,ZP0,3}, {"???",XXX,IMP,3}, {"TAY",TAY,IMP,2}, {"LDA",LDA,IMM,2}, {"TAX",TAX,IMP,2}, {"???",XXX,IMP,2}, {"LDY",LDY,ABS,4}, {"LDA",LDA,ABS,4}, {"LDX",LDX,ABS,4}, {"???",XXX,IMP,4},
	{"BCS",BCS,REL,2}, {"LDA",LDA,IZY,5}, {"???",XXX,IMP,2}, {"???",XXX,IMP,5}, {"LDY",LDY,ZPX,4}, {"LDA",LDA,ZPX,4}, {"LDX",LDX,ZPY,4}, {"???",XXX,IMP,4}, {"CLV",CLV,IMP,2}, {"LDA",LDA,ABY,4}, {"TSX",TSX,IMP,2}, {"???",XXX,IMP,4}, {"LDY",LDY,ABX,4}, {"LDA",LDA,ABX,4}, {"LDX",LDX,ABY,4}, {"???",XXX,IMP,4},
	{"CPY",CPY,IMM,2}, {"CMP",CMP,IZX,6}, {"???",NOP,IMP,2}, {"???",XXX,IMP,8}, {"CPY",CPY,ZP0,3}, {"CMP",CMP,ZP0,3}, {"DEC",DEC,ZP0,5}, {"???",XXX,IMP,5}, {"INY",INY,IMP,2}, {"CMP",CMP,IMM,2}, {"DEX",DEX,IMP,2}, {"???",XXX,IMP,2}, {"CPY",CPY,ABS,4}, {"CMP",CMP,ABS,4}, {"DEC",DEC,ABS,6}, {"???",XXX,IMP,6},
	{"BNE",BNE,REL,2}, {"CMP",CMP,IZY,5}, {"???",XXX,IMP,2}, {"???",XXX,IMP,8}, {"???",NOP,IMP,4}, {"CMP",CMP,ZPX,4}, {"DEC",DEC,ZPX,6}, {"???",XXX,IMP,6}, {"CLD",CLD,IMP,2}, {"CMP",CMP,ABY,4}, {"NOP",NOP,IMP,2}, {"???",XXX,IMP,7}, {"???",NOP,IMP,4}, {"CMP",CMP,ABX,4}, {"DEC",DEC,ABX,7}, {"???",XXX,IMP,7},
	{"CPX",CPX,IMM,2}, {"SBC",SBC,IZX,6}, {"???",NOP,IMP,2}, {"???",XXX,IMP,8}, {"CPX",CPX,ZP0,3}, {"SBC",SBC,ZP0,3}, {"INC",INC,ZP0,5}, {"???",XXX,IMP,5}, {"INX",INX,IMP,2}, {"SBC",SBC,IMM,2}, {"NOP",NOP,IMP,2}, {"???",SBC,IMP,2}, {"CPX",CPX,ABS,4}, {"SBC",SBC,ABS,4}, {"INC",INC,ABS,6}, {"???",XXX,IMP,6},
	{"BEQ",BEQ,REL,2}, {"SBC",SBC,IZY,5}, {"???",XXX,IMP,2}, {"???",XXX,IMP,8}, {"???",NOP,IMP,4}, {"SBC",SBC,ZPX,4}, {"INC",INC,ZPX,6}, {"???",XXX,IMP,6}, {"SED",SED,IMP,2}, {"SBC",SBC,ABY,4}, {"NOP",NOP,IMP,2}, {"???",XXX,IMP,7}, {"???",NOP,IMP,4}, {"SBC",SBC,ABX,4}, {"INC",INC,ABX,7}, {"???",XXX,IMP,7},
};

//static Instruction opcodes[256] = {
//	{"BRK",IMP,BRK}, {"ORA",IZX,ORA}, {"XXX",IMP,XXX}, {"XXX",IMP,XXX}, {"XXX",IMP,XXX}, {"ORA",ZP0,ORA}, {"ASL",ZP0,ASL}, {"XXX",IMP,XXX},
//	{"PHP",IMP,PHP}, {"ORA",IMM,ORA}, {"ASL",ACC,ASL}, {"XXX",IMP,XXX}, {"XXX",IMP,XXX}, {"ORA",ABS,ORA}, {"ASL",ABS,ASL}, {"XXX",IMP,XXX},
//	{"BPL",REL,BPL}, {"ORA",IZY,ORA}, {"XXX",IMP,XXX}, {"XXX",IMP,XXX}, {"XXX",IMP,XXX}, {"ORA",ZPX,ORA}, {"ASL",ZPX,ASL}, {"XXX",IMP,XXX},
//	{"CLC",IMP,CLC}, {"ORA",ABY,ORA}, {"XXX",IMP,XXX}, {"XXX",IMP,XXX}, {"XXX",IMP,XXX}, {"ORA",ABX,ORA}, {"ASL",ABX,ASL}, {"XXX",IMP,XXX},
//	{"JSR",ABS,JSR}, {"AND",IZX,AND}, {"XXX",IMP,XXX}, {"XXX",IMP,XXX}, {"BIT",ZP0,BIT}, {"AND",ZP0,AND}, {"ROL",ZP0,ROL}, {"XXX",IMP,XXX},
//	{"PLP",IMP,PLP}, {"AND",IMM,AND}, {"ROL",ACC,ROL}, {"XXX",IMP,XXX}, {"BIT",ABS,BIT}, {"AND",ABS,AND}, {"ROL",ABS,ROL}, {"XXX",IMP,XXX},
//	{"BMI",REL,BMI}, {"AND",IZY,AND}, {"XXX",IMP,XXX}, {"XXX",IMP,XXX}, {"XXX",IMP,XXX}, {"AND",ZPX,AND}, {"ROL",ZPX,ROL}, {"XXX",IMP,XXX},
//	{"SEC",IMP,SEC}, {"AND",ABY,AND}, {"XXX",IMP,XXX}, {"XXX",IMP,XXX}, {"XXX",IMP,XXX}, {"AND",ABX,AND},
//};
