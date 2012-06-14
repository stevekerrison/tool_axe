// Copyright (c) 2012, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#include <cstdlib>
#include "Thread.h"
#include "Core.h"
#include "Chanend.h"
#include "BitManip.h"
#include "CRC.h"
#include "InstructionHelpers.h"
#include "Exceptions.h"
#include "Synchroniser.h"
#include "InstructionMacrosCommon.h"
#include "SyscallHandler.h"
#include <cstdio>

/// Use to get the required LLVM type for instruction functions.
extern "C" JITReturn jitInstructionTemplate(Thread &t) {
  return JIT_RETURN_CONTINUE;
}

extern "C" uint32_t jitGetPc(Thread &t) {
  return t.pc;
}

extern "C" JITReturn jitStubImpl(Thread &t) {
  if (t.getParent().updateExecutionFrequencyFromStub(t.pc)) {
    t.pendingPc = t.pc;
    t.pc = t.getParent().getRunJitAddr();
  }
  return JIT_RETURN_END_TRACE;
}

extern "C" void jitUpdateExecutionFrequency(Thread &t) {
  t.getParent().updateExecutionFrequency(t.pc);
}

extern "C" uint32_t
jitComputeAddress(const Thread &t, Register::Reg baseReg, unsigned scale,
                  Register::Reg offsetReg, uint32_t immOffset)
{
  uint32_t address = t.regs[baseReg];
  if (scale != 0)
    address += scale * t.regs[offsetReg];
  address += immOffset;
  return address;
}

extern "C" bool
jitCheckAddress(const Thread &t, uint32_t ramSizeLog2, uint32_t address)
{
  return (address >> ramSizeLog2) == t.getParent().ramBaseMultiple;
}

extern "C" bool jitInvalidateByteCheck(Thread &t, uint32_t address)
{
  return t.getParent().invalidateByteCheck(address);
}

extern "C" bool jitInvalidateShortCheck(Thread &t, uint32_t address)
{
  return t.getParent().invalidateShortCheck(address);
}

extern "C" bool jitInvalidateWordCheck(Thread &t, uint32_t address)
{
  return t.getParent().invalidateWordCheck(address);
}

extern "C" JITReturn jitInterpretOne(Thread &t) {
  t.pendingPc = t.pc;
  t.pc = t.getParent().getInterpretOneAddr();
  return JIT_RETURN_END_TRACE;
}

#define THREAD thread
#define CORE THREAD.getParent()
#define PHYSICAL_ADDR(addr) ((addr) - ramBase)
#define VIRTUAL_ADDR(addr) ((addr) + ramBase)
#define CHECK_ADDR(addr) ((uint32_t(addr) >> ramSizeLog2) == CORE.ramBaseMultiple)
#define CHECK_PC(addr) ((uint32_t(addr) >> (ramSizeLog2 - 1)) == 0)
//#define ERROR() internalError(THREAD, __FILE__, __LINE__);
#define ERROR() std::abort();
#define OP(n) (field ## n)
#define LOP(n) OP(n)
#define EMIT_JIT_INSTRUCTION_FUNCTIONS
#include "InstructionGenOutput.inc"
#undef EMIT_JIT_INSTRUCTION_FUNCTIONS
