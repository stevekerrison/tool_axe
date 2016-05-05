// Copyright (c) 2012, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#include "LLVMExtra.h"
#include "llvm-c/Disassembler.h"
#include "llvm-c/Target.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#if ((LLVM_MAJOR_VERSION > 3) || (LLVM_VERSION_MINOR >= 5))
    #include "llvm/IR/CallSite.h"
#else
    #include "llvm/Support/CallSite.h"
#endif
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/IR/LegacyPassManager.h"
#include <iostream>

using namespace llvm;

LLVMBool LLVMExtraInlineFunction(LLVMValueRef call)
{
  InlineFunctionInfo IFI;
  return InlineFunction(CallSite(unwrap(call)), IFI);
}

#if 0
class JitDisassembler : public JITEventListener {
  LLVMDisasmContextRef DC;
public:
  JitDisassembler(const char *triple) {
    LLVMInitializeX86Disassembler();
    DC = LLVMCreateDisasm(triple, 0, 0, 0, 0);
  }
  ~JitDisassembler() {
    LLVMDisasmDispose(DC);
  }
  void NotifyFunctionEmitted(const Function &f, void *code, size_t size,
                             const EmittedFunctionDetails &) override {
    uint8_t *bytes = (uint8_t*)code;
    uint8_t *end = bytes + size;
    char buf[256];
    while (bytes < end) {
      size_t instSize = LLVMDisasmInstruction(DC, bytes, end - bytes,
                                              (uint64_t)bytes, &buf[0], 256);
      if (instSize == 0) {
        std::cerr << "\t???\n";
        return;
      }
      bytes += instSize;
      std::cerr << buf << '\n';
    }
  }
};
#endif

void
LLVMExtraRegisterJitDisassembler(LLVMExecutionEngineRef EE,
                                 const char *triple) {
#if 0
  static JitDisassembler disassembler(triple);
  unwrap(EE)->RegisterJITEventListener(&disassembler);
#endif
}

void LLVMDisableSymbolSearching(LLVMExecutionEngineRef EE, LLVMBool Disable)
{
  unwrap(EE)->DisableSymbolSearching(Disable);
}
