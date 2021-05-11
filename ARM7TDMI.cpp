#include "ARM7TDMI.h"
#include "Bus.h"
#include <bit>
#include <bitset>
#include <iostream>


ARM7TDMI::ARM7TDMI() {
    cpsr.Mode = SUPERVISOR;
    cpsr.T = 0; // set to ARM state
    setRegister(PC_REGISTER, BOOT_LOCATION);
}


ARM7TDMI::~ARM7TDMI() {
}


void ARM7TDMI::step() {
    // read from program counter
    uint32_t rawInstruction = bus->read(registers[PC_REGISTER]);

    if(cpsr.T) { // check state bit, is CPU in ARM state?
        Cycles cycles = executeInstruction(rawInstruction);

    } else { // THUMB state

    }
}


void ARM7TDMI::connectBus(Bus* bus) {
    this->bus = bus;
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ ALU OPERATIONS ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
// TODO: put assertions for every unexpected or unallowed circumstance (for deubgging)
// TODO: cycle calculation

ARM7TDMI::Cycles ARM7TDMI::executeAluInstruction(uint32_t instruction) {
    AluShiftResult shiftResult = aluShift(instruction, (instruction & 0x02000000), (instruction & 0x00000010));
    uint8_t rd = (instruction & 0x0000F000) >> 12;
    uint8_t rn = (instruction & 0x000F0000) >> 16;
    uint8_t opcode = instruction & 0x01E00000 >> 21;

    uint32_t rnValue;
    if(rn != PC_REGISTER) {
        rnValue = getRegister(rn);
    } else if(!(instruction & 0x02000000) && (instruction & 0x00000010)) {
        rnValue = getRegister(rn) + 12;
    } else {
        rnValue = getRegister(rn) + 8;
    }
    uint32_t op2 = shiftResult.op2;

    executeAluInstructionOperation(opcode, rd, getRegister(rn), op2);

    if(rd != PC_REGISTER && (instruction & 0x00100000)) {
        cpsr.C = carryBit;
        cpsr.Z = overflowBit;
        cpsr.N = signBit;
        cpsr.V = overflowBit;
    } else if(rd == PC_REGISTER && (instruction & 0x00100000)) {
        cpsr = getModeSpsr();
    } else { } // flags not affected, not allowed in CMP
}


/*
    0: AND{cond}{S} Rd,Rn,Op2    ;AND logical       Rd = Rn AND Op2
    1: EOR{cond}{S} Rd,Rn,Op2    ;XOR logical       Rd = Rn XOR Op2
    2: SUB{cond}{S} Rd,Rn,Op2 ;* ;subtract          Rd = Rn-Op2
    3: RSB{cond}{S} Rd,Rn,Op2 ;* ;subtract reversed Rd = Op2-Rn
    4: ADD{cond}{S} Rd,Rn,Op2 ;* ;add               Rd = Rn+Op2
    5: ADC{cond}{S} Rd,Rn,Op2 ;* ;add with carry    Rd = Rn+Op2+Cy
    6: SBC{cond}{S} Rd,Rn,Op2 ;* ;sub with carry    Rd = Rn-Op2+Cy-1
    7: RSC{cond}{S} Rd,Rn,Op2 ;* ;sub cy. reversed  Rd = Op2-Rn+Cy-1
    8: TST{cond}{P}    Rn,Op2    ;test            Void = Rn AND Op2
    9: TEQ{cond}{P}    Rn,Op2    ;test exclusive  Void = Rn XOR Op2
    A: CMP{cond}{P}    Rn,Op2 ;* ;compare         Void = Rn-Op2
    B: CMN{cond}{P}    Rn,Op2 ;* ;compare neg.    Void = Rn+Op2
    C: ORR{cond}{S} Rd,Rn,Op2    ;OR logical        Rd = Rn OR Op2
    D: MOV{cond}{S} Rd,Op2       ;move              Rd = Op2
    E: BIC{cond}{S} Rd,Rn,Op2    ;bit clear         Rd = Rn AND NOT Op2
    F: MVN{cond}{S} Rd,Op2       ;not               Rd = NOT Op2
*/
ARM7TDMI::Cycles ARM7TDMI::executeAluInstructionOperation(uint8_t opcode, uint32_t rd, uint32_t rnVal, uint32_t op2) {
    switch(opcode) {
        case AND: { // AND
            uint32_t result = rnVal & op2;
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
        }
        case EOR: { // EOR
            uint32_t result = rnVal ^ op2;
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
        }
        case SUB: { // SUB
            uint32_t result = rnVal - op2;
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = aluSubtractSetsCarryBit(rnVal, op2);
            overflowBit = aluSubtractSetsOverflowBit(rnVal, op2, result);
        }
        case RSB: { // RSB
            uint32_t result = op2 - rnVal;
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = aluSubtractSetsCarryBit(op2, rnVal);
            overflowBit = aluSubtractSetsOverflowBit(op2, rnVal, result);
        }
        case ADD: { // ADD
            uint32_t result = rnVal + op2;
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = aluAddSetsCarryBit(rnVal, op2);
            overflowBit = aluAddSetsOverflowBit(rnVal, op2, result);
        } 
        case ADC: { // ADC
            uint64_t result = (uint64_t)(rnVal + op2 + cpsr.C);
            setRegister(rd, (uint32_t)result);
            zeroBit = aluSetsZeroBit((uint32_t)result);
            signBit = aluSetsSignBit((uint32_t)result);
            carryBit = aluAddWithCarrySetsCarryBit(result);
            overflowBit = aluAddWithCarrySetsOverflowBit(rnVal, op2, (uint32_t)result);
        }
        case SBC: { // SBC
            uint64_t result = rnVal + (~op2) + cpsr.C;
            setRegister(rd, (uint32_t)result);
            zeroBit = aluSetsZeroBit((uint32_t)result);
            signBit = aluSetsSignBit((uint32_t)result);
            carryBit = aluSubWithCarrySetsCarryBit(result);
            overflowBit = aluSubWithCarrySetsOverflowBit(rnVal, op2, (uint32_t)result);
        }
        case RSC: { // RSC
            uint64_t result = op2 + (~rnVal) + cpsr.C;
            setRegister(rd, (uint32_t)result);
            zeroBit = aluSetsZeroBit((uint32_t)result);
            signBit = aluSetsSignBit((uint32_t)result);
            carryBit = aluSubWithCarrySetsCarryBit(result);
            overflowBit = aluSubWithCarrySetsOverflowBit(op2, rnVal, (uint32_t)result);
        }
        case TST: { // TST
            uint32_t result = rnVal & op2;
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
        }
        case TEQ: { // TEQ
            uint32_t result = rnVal ^ op2;
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
        }
        case CMP: { // CMP
            uint32_t result = rnVal - op2;
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = aluSubtractSetsCarryBit(rnVal, op2);
            overflowBit = aluSubtractSetsOverflowBit(rnVal, op2, result);
        }
        case CMN: { // CMN
            uint32_t result = rnVal + op2;
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
            carryBit = aluAddSetsCarryBit(rnVal, op2);
            overflowBit = aluAddSetsOverflowBit(rnVal, op2, result);
        }
        case ORR: { // ORR
            uint32_t result = rnVal | op2;
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
        }
        case MOV: { // MOV
            uint32_t result = op2;
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
        }
        case BIC: { // BIC
            uint32_t result = rnVal & (~op2);
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
        }
        case MVN: { // MVN
            uint32_t result = ~op2;
            setRegister(rd, result);
            zeroBit = aluSetsZeroBit(result);
            signBit = aluSetsSignBit(result);
        }
    }
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ END OF ALU OPERATIONS ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/



ARM7TDMI::Cycles ARM7TDMI::UNDEF(uint32_t instruction) {
    return {};
}


/*
ARM Binary Opcode Format
    |..3 ..................2 ..................1 ..................0|
    |1_0_9_8_7_6_5_4_3_2_1_0_9_8_7_6_5_4_3_2_1_0_9_8_7_6_5_4_3_2_1_0|
    |_Cond__|0_0_0|___Op__|S|__Rn___|__Rd___|__Shift__|Typ|0|__Rm___| DataProc
    |_Cond__|0_0_0|___Op__|S|__Rn___|__Rd___|__Rs___|0|Typ|1|__Rm___| DataProc
    |_Cond__|0_0_1|___Op__|S|__Rn___|__Rd___|_Shift_|___Immediate___| DataProc
    |_Cond__|0_0_1_1_0|P|1|0|_Field_|__Rd___|_Shift_|___Immediate___| PSR Imm
    |_Cond__|0_0_0_1_0|P|L|0|_Field_|__Rd___|0_0_0_0|0_0_0_0|__Rm___| PSR Reg
    |_Cond__|0_0_0_1_0_0_1_0_1_1_1_1_1_1_1_1_1_1_1_1|0_0|L|1|__Rn___| BX,BLX
    |_Cond__|0_0_0_0_0_0|A|S|__Rd___|__Rn___|__Rs___|1_0_0_1|__Rm___| Multiply
    |_Cond__|0_0_0_0_1|U|A|S|_RdHi__|_RdLo__|__Rs___|1_0_0_1|__Rm___| MulLong
    |_Cond__|0_0_0_1_0|Op_|0|Rd/RdHi|Rn/RdLo|__Rs___|1|y|x|0|__Rm___| MulHalfARM9
    |_Cond__|0_0_0|P|U|0|W|L|__Rn___|__Rd___|0_0_0_0|1|S|H|1|__Rm___| TransReg10
    |_Cond__|0_0_0|P|U|1|W|L|__Rn___|__Rd___|OffsetH|1|S|H|1|OffsetL| TransImm10
    |_Cond__|0_1_0|P|U|B|W|L|__Rn___|__Rd___|_________Offset________| TransImm9
    |_Cond__|0_1_1|P|U|B|W|L|__Rn___|__Rd___|__Shift__|Typ|0|__Rm___| TransReg9
    |_Cond__|0_1_1|________________xxx____________________|1|__xxx__| Undefined
    |_Cond__|1_0_0|P|U|S|W|L|__Rn___|__________Register_List________| BlockTrans
    |_Cond__|1_0_1|L|___________________Offset______________________| B,BL,BLX
    |_Cond__|1_1_1_1|_____________Ignored_by_Processor______________| SWI           
*/  

// TODO: find a way to write a decoder that is as FAST and SIMPLE as possible
ARM7TDMI::Cycles ARM7TDMI::executeInstruction(uint32_t instruction) {
    // highest possible specificity to lowest specifity
    switch(instruction & 0x010E00000) {
        case 0x0:
        case 0x1:
        case 0x2:
        case 0x3:
        case 0x4: 
        case 0x5:
        case 0x6:
        case 0x7:
        case 0x8: 
        case 0x9: 
        case 0xA:
        case 0xB:
        case 0xC:
        case 0xD:
        case 0xE:
        case 0xF:
            break;
    }
    return {};
}


// Comment documentation sourced from the ARM7TDMI Data Sheet. 
// TODO: potential optimization (?) only return carry bit and just shift the op2 in the instruction itself
ARM7TDMI::AluShiftResult ARM7TDMI::aluShift(uint32_t instruction, bool i, bool r) {
    if(i) { // shifted immediate value as 2nd operand
        /*
            The immediate operand rotate field is a 4 bit unsigned integer 
            which specifies a shift operation on the 8 bit immediate value. 
            This value is zero extended to 32 bits, and then subject to a 
            rotate right by twice the value in the rotate field.
        */
        uint32_t rm = instruction & 0x000000FF;
        uint8_t is = (instruction & 0x00000F00) >> 7U;
        uint32_t op2 = aluShiftRor(rm, is % 32);
        // carry out bit is the least significant discarded bit of rm
        if(is > 0) {
            carryBit = (rm >> (is - 1)); 
        }
        return {op2, carryBit};
    }

    /* ~~~~~~~~~ else: shifted register value as 2nd operand ~~~~~~~~~~ */
    uint8_t shiftType = (instruction & 0x00000060) >> 5U;
    uint32_t op2;
    uint8_t rmIndex = instruction & 0x0000000F;
    uint32_t rm = getRegister(rmIndex);
    // see comment in opcode functions for explanation why we're doing this
    if(rmIndex != PC_REGISTER) {
        // do nothing
    } else if(!i && r) {
        rm += 12;
    } else {
        rm += 8;
    }

    uint32_t shiftAmount;

    if(r) { // register as shift amount
        uint8_t rsIndex = (instruction & 0x00000F00) >> 8U;
        assert(rsIndex != 15);
        shiftAmount = getRegister(rsIndex) & 0x000000FF;
    } else { // immediate as shift amount
        shiftAmount = instruction & 0x00000F80 >> 7U;
    }

    bool immOpIsZero = r ? false : shiftAmount == 0;

    if(shiftType == 0) {  // Logical Shift Left
        /*
            A logical shift left (LSL) takes the contents of
            Rm and moves each bit by the specified amount 
            to a more significant position. The least significant 
            bits of the result are filled with zeros, and the high bits 
            of Rm which do not map into the result are discarded, except 
            that the least significant discarded bit becomes the shifter
            carry output which may be latched into the C bit of the CPSR 
            when the ALU operation is in the logical class
        */
        if(!immOpIsZero) {
            op2 = aluShiftLsl(rm, shiftAmount);
            carryBit = (rm >> (32 - shiftAmount)) & 1;
        } else { // no op performed, carry flag stays the same
            op2 = rm;
            carryBit = cpsr.C;
        }
    }
    else if(shiftType == 1) { // Logical Shift Right
        /*
            A logical shift right (LSR) is similar, but the contents 
            of Rm are moved to less significant positions in the result    
        */
        if(!immOpIsZero) {
            op2 = aluShiftLsr(rm, shiftAmount);
            carryBit = (rm >> (shiftAmount - 1)) & 1;
        } else {
            /*
                The form of the shift field which might be expected to 
                correspond to LSR #0 is used to encode LSR #32, which has a 
                zero result with bit 31 of Rm as the carry output
            */
            op2 = 0;
            carryBit = rm >> 31;
        }
    }
    else if(shiftType == 2) { // Arithmetic Shift Right
        /*
            An arithmetic shift right (ASR) is similar to logical shift right, 
            except that the high bits are filled with bit 31 of Rm instead of zeros. 
            This preserves the sign in 2's complement notation
        */
        if(!immOpIsZero) {
            op2 = aluShiftAsr(rm, shiftAmount);
            carryBit = rm >> 31;
        } else {
            /*
                The form of the shift field which might be expected to give ASR #0 
                is used to encode ASR #32. Bit 31 of Rm is again used as the carry output, 
                and each bit of operand 2 is also equal to bit 31 of Rm. 
            */
            op2 = aluShiftAsr(rm, 32);
            carryBit = rm >> 31;
        }
    }
    else { // Rotating Shift
        /*
            Rotate right (ROR) operations reuse the bits which “overshoot” 
            in a logical shift right operation by reintroducing them at the
            high end of the result, in place of the zeros used to fill the high 
            end in logical right operation
        */
        if(!immOpIsZero) {
            op2 = aluShiftRor(rm, shiftAmount % 32);
            carryBit = (rm >> (shiftAmount - 1)) & 1;
        } else {
            /*
                The form of the shift field which might be expected to give ROR #0 
                is used to encode a special function of the barrel shifter, 
                rotate right extended (RRX). This is a rotate right by one bit position 
                of the 33 bit quantity formed by appending the CPSR C flag to the most 
                significant end of the contents of Rm as shown
            */
            op2 = rm >> 1;
            op2 = op2 | (((uint32_t)cpsr.C) << 31);
            carryBit = rm & 1;
        }
    }

    return {op2, carryBit};
}


// TODO: make sure all these shift functions are correct
uint32_t ARM7TDMI::aluShiftLsl(uint32_t value, uint8_t shift) {
    return value << shift;
}


uint32_t ARM7TDMI::aluShiftLsr(uint32_t value, uint8_t shift) {
    return value >> shift;
}


uint32_t ARM7TDMI::aluShiftAsr(uint32_t value, uint8_t shift) {
    // assuming that the compiler implements arithmetic right shift on signed ints 
    // TODO: make more portable
    return (uint32_t)(((int32_t)value) >> shift);
}


uint32_t ARM7TDMI::aluShiftRor(uint32_t value, uint8_t shift) {
    assert (shift < 32U);
    return (value >> shift) | (value << (-shift & 31U));
}


uint32_t ARM7TDMI::aluShiftRrx(uint32_t value, uint8_t shift) {
    assert (shift < 32U);
    uint32_t rrxMask = cpsr.C;
    return ((value >> shift) | (value << (-shift & 31U))) | (rrxMask << 31U);
}


ARM7TDMI::ProgramStatusRegister ARM7TDMI::getModeSpsr() {
    if(cpsr.Mode == USER) {
        return cpsr;
    } else if(cpsr.Mode == FIQ) {
        return SPSR_fiq;
    } else if(cpsr.Mode == IRQ) {
        return SPSR_irq;
    } else if(cpsr.Mode == SUPERVISOR) {
        return SPSR_svc;
    } else if(cpsr.Mode == ABORT) {
        return SPSR_abt;
    } else if(cpsr.Mode == UNDEFINED) {
        return SPSR_und;
    } else {
        return cpsr;
    }
}


uint32_t ARM7TDMI::getRegister(uint8_t index) {
    if(cpsr.Mode == USER) {
        return registers[index];
    } else if(cpsr.Mode == FIQ) {
        return *(fiqRegisters[index]);
    } else if(cpsr.Mode == IRQ) {
        return *(irqRegisters[index]);
    } else if(cpsr.Mode == SUPERVISOR) {
        return *(svcRegisters[index]);
    } else if(cpsr.Mode == ABORT) {
        return *(abtRegisters[index]);
    } else if(cpsr.Mode == UNDEFINED) {
        return *(undRegisters[index]);
    } else {
        return registers[index];
    }
}


void ARM7TDMI::setRegister(uint8_t index, uint32_t value) {
    if(cpsr.Mode == USER) {
        registers[index] = value;
    } else if(cpsr.Mode == FIQ) {
        *(fiqRegisters[index]) = value;
    } else if(cpsr.Mode == IRQ) {
        *(irqRegisters[index]) = value;
    } else if(cpsr.Mode == SUPERVISOR) {
        *(svcRegisters[index]) = value;
    } else if(cpsr.Mode == ABORT) {
        *(abtRegisters[index]) = value;
    } else if(cpsr.Mode == UNDEFINED) {
        *(undRegisters[index]) = value;
    } else {
        registers[index] = value;
    }
}





bool ARM7TDMI::aluSetsZeroBit(uint32_t value) {
    return value == 0;
}

bool ARM7TDMI::aluSetsSignBit(uint32_t value) {
    return value >> 31;
}

bool ARM7TDMI::aluSubtractSetsCarryBit(uint32_t rnValue, uint32_t op2) {
    return !(rnValue < op2);
}

bool ARM7TDMI::aluSubtractSetsOverflowBit(uint32_t rnValue, uint32_t op2, uint32_t result) {
    // todo: maybe there is a more efficient way to do this
    return (!(rnValue & 0x80000000) && (op2 & 0x80000000) && (result & 0x80000000)) || 
            ((rnValue & 0x80000000) && !(op2 & 0x80000000) && !(result & 0x80000000));
}

bool ARM7TDMI::aluAddSetsCarryBit(uint32_t rnValue, uint32_t op2) {
    return (0xFFFFFFFF - op2) < rnValue;
}

bool ARM7TDMI::aluAddSetsOverflowBit(uint32_t rnValue, uint32_t op2, uint32_t result) {
    // todo: maybe there is a more efficient way to do this
    return ((rnValue & 0x80000000) && (op2 & 0x80000000) && !(result & 0x80000000)) || 
            (!(rnValue & 0x80000000) && !(op2 & 0x80000000) && (result & 0x80000000));
}

bool ARM7TDMI::aluAddWithCarrySetsCarryBit(uint64_t result) {
    return result >> 32;
}

bool ARM7TDMI::aluAddWithCarrySetsOverflowBit(uint32_t rnValue, uint32_t op2, uint32_t result) {
    // todo: maybe there is a more efficient way to do this
    return   ((rnValue & 0x80000000) && (op2 & 0x80000000) && !((rnValue + op2) & 0x80000000)) || 
             (!(rnValue & 0x80000000) && !(op2 & 0x80000000) && ((rnValue + op2) & 0x80000000)) || 
            // ((rnValue + op2) & 0x80000000) && (cpsr.C & 0x80000000) && !(((uint32_t)result) & 0x80000000)) ||  never happens
             (!((rnValue + op2) & 0x80000000) && !(cpsr.C & 0x80000000) && ((result) & 0x80000000));
}

bool ARM7TDMI::aluSubWithCarrySetsCarryBit(uint64_t result) {
    return !(result >> 32);
}

bool ARM7TDMI::aluSubWithCarrySetsOverflowBit(uint32_t rnValue, uint32_t op2, uint32_t result) {
    // todo: maybe there is a more efficient way to do this
    return ((rnValue & 0x80000000) && ((~op2) & 0x80000000) && !((rnValue + (~op2)) & 0x80000000)) || 
            (!(rnValue & 0x80000000) && !((~op2) & 0x80000000) && ((rnValue + (~op2)) & 0x80000000)) || 
             // (((rnValue + (~op2)) & 0x80000000) && (cpsr.C & 0x80000000) && !(result & 0x80000000)) || never happens
            (!((rnValue + (~op2)) & 0x80000000) && !(cpsr.C & 0x80000000) && (result & 0x80000000));
}
