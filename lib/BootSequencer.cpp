// Copyright (c) 2012, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#include "BootSequencer.h"
#include "Core.h"
#include "ProcessorNode.h"
#include "SystemState.h"
#include "SyscallHandler.h"
#include "XE.h"
#include "ScopedArray.h"
#include "StopReason.h"
#include "SymbolInfo.h"
#include "Tracer.h"
#include <gelf.h>
#include <iostream>
#include <algorithm>

using namespace axe;

const unsigned XCORE_ELF_MACHINE_OLD = 0xB49E;
const unsigned XCORE_ELF_MACHINE = 0xCB;

static void readSymbols(Elf *e, Elf_Scn *scn, const GElf_Shdr &shdr,
                        unsigned low, unsigned high,
                        std::auto_ptr<CoreSymbolInfo> &SI)
{
  Elf_Data *data = elf_getdata(scn, NULL);
  if (data == NULL) {
    return;
  }
  unsigned count = shdr.sh_size / shdr.sh_entsize;
  
  CoreSymbolInfoBuilder builder;
  
  for (unsigned i = 0; i < count; i++) {
    GElf_Sym sym;
    if (gelf_getsym(data, i, &sym) == NULL) {
      continue;
    }
    if (sym.st_shndx == SHN_ABS)
      continue;
    if (sym.st_value < low || sym.st_value >= high)
      continue;
    builder.addSymbol(elf_strptr(e, shdr.sh_link, sym.st_name),
                      sym.st_value,
                      sym.st_info);
  }
  SI = builder.getSymbolInfo();
}

static void readSymbols(Elf *e, unsigned low, unsigned high,
                        std::auto_ptr<CoreSymbolInfo> &SI)
{
  Elf_Scn *scn = NULL;
  GElf_Shdr shdr;
  while ((scn = elf_nextscn(e, scn)) != NULL) {
    if (gelf_getshdr(scn, &shdr) == NULL) {
      continue;
    }
    if (shdr.sh_type == SHT_SYMTAB) {
      // Found the symbol table
      break;
    }
  }

  if (scn != NULL) {
    readSymbols(e, scn, shdr, low, high, SI);
  }
}

namespace {
  class LoadedElf {
    char *buf;
    Elf *elf;
  public:
    LoadedElf(const XEElfSector *elfSector);
    LoadedElf(const LoadedElf &) = delete;
    LoadedElf &operator=(const LoadedElf &) = delete;
    ~LoadedElf();
    Elf *getElf() const { return elf; }
    const char *getBuf() const { return buf; }
  };
  class ElfManager {
    std::map<Core*, LoadedElf*> loadedElfMap;
  public:
    ~ElfManager();
    ElfManager() = default;
    ElfManager(const ElfManager &) = delete;
    ElfManager &operator=(const ElfManager &) = delete;
    const LoadedElf &load(Core &core, const XEElfSector *sector);
    const LoadedElf *getLoadedElf(Core &core);
  };
  struct ExecutionState {
    SystemState &sys;
    BreakpointManager &breakpointManager;
    SyscallHandler &syscallHandler;
    ElfManager &elfManager;
    ExecutionState(SystemState &s, BreakpointManager &BM, SyscallHandler &SH,
                   ElfManager &EM) :
      sys(s),
      breakpointManager(BM),
      syscallHandler(SH),
      elfManager(EM) { }
  };
}

LoadedElf::LoadedElf(const XEElfSector *elfSector) :
  buf(new char[elfSector->getElfSize()])
{
  if (!elfSector->getElfData(buf)) {
    std::cerr << "Error reading ELF data from ELF sector" << std::endl;
    std::exit(1);
  }
  if ((elf = elf_memory(buf, elfSector->getElfSize())) == NULL) {
    std::cerr << "Error reading ELF: " << elf_errmsg(-1) << std::endl;
    std::exit(1);
  }
}

LoadedElf::~LoadedElf()
{
  delete[] buf;
  if (elf)
    elf_end(elf);
}

ElfManager::~ElfManager()
{
  for (const auto &entry : loadedElfMap) {
    delete entry.second;
  }
}

const LoadedElf &ElfManager::load(Core &core, const XEElfSector *elfSector)
{
  LoadedElf *&loadedElf = loadedElfMap[&core];
  delete loadedElf;
  loadedElf = new LoadedElf(elfSector);
  return *loadedElf;
}

const LoadedElf *ElfManager::getLoadedElf(Core &core)
{
  auto match = loadedElfMap.find(&core);
  if (match == loadedElfMap.end())
    return nullptr;
  return match->second;
}

class axe::BootSequenceStep {
public:
  enum Type {
    ELF,
    SCHEDULE,
    RUN,
  };
private:
  Type type;
protected:
  BootSequenceStep(Type t) : type(t) {}
public:
  virtual ~BootSequenceStep() {}
  Type getType() const { return type; }
  virtual int execute(ExecutionState &state) = 0;
};

class BootSequenceStepElf : public BootSequenceStep {
  bool loadImage;
  bool useElfEntryPoint;
  Core *core;
  const XEElfSector *elfSector;
public:
  BootSequenceStepElf(Core *c, const XEElfSector *e) :
    BootSequenceStep(ELF),
    loadImage(true),
    useElfEntryPoint(true),
    core(c),
    elfSector(e) {}
  int execute(ExecutionState &state) override;
  Core *getCore() const { return core; }
  void setUseElfEntryPoint(bool value) { useElfEntryPoint = value; }
  void setLoadImage(bool value) { loadImage = value; }
};

class BootSequenceStepSchedule : public BootSequenceStep {
  Core *core;
  uint32_t address;
public:
  BootSequenceStepSchedule(Core *c, uint32_t a) :
    BootSequenceStep(SCHEDULE),
    core(c),
    address(a) {}
  int execute(ExecutionState &state) override;
};

class BootSequenceStepRun : public BootSequenceStep {
  unsigned numDoneSyscalls;
  int executeAux(ExecutionState &state);
public:
  BootSequenceStepRun(unsigned n) :
    BootSequenceStep(RUN),
    numDoneSyscalls(n) {}
  int execute(ExecutionState &state) override;
};

static bool hasValidVirtualAddress(const GElf_Phdr &phdr, const Core &core)
{
  return core.isValidRamAddress(phdr.p_vaddr) &&
         core.isValidRamAddress(phdr.p_vaddr + phdr.p_memsz);
 }

int BootSequenceStepElf::execute(ExecutionState &state)
{
  SystemState &sys = state.sys;
  BreakpointManager &BM = state.breakpointManager;
  const LoadedElf &loadedElf = state.elfManager.load(*core, elfSector);
  Elf *e = loadedElf.getElf();
  const char *buf = loadedElf.getBuf();
  if (elf_kind(e) != ELF_K_ELF) {
    std::cerr << "ELF section is not an ELF object" << std::endl;
    std::exit(1);
  }
  GElf_Ehdr ehdr;
  if (gelf_getehdr(e, &ehdr) == NULL) {
    std::cerr << "Reading ELF header failed: " << elf_errmsg(-1) << std::endl;
    std::exit(1);
  }
  if (ehdr.e_machine != XCORE_ELF_MACHINE &&
      ehdr.e_machine != XCORE_ELF_MACHINE_OLD) {
    std::cerr << "Not a XCore ELF" << std::endl;
    std::exit(1);
  }
  uint32_t entryPoint = core->getRamBase();
  if (ehdr.e_entry != 0) {
    if (core->isValidRamAddress(ehdr.e_entry)) {
      entryPoint = ehdr.e_entry;
    } else {
      std::cout << "Warning: invalid ELF entry point 0x";
      std::cout << std::hex << ehdr.e_entry << std::dec << "\n";
    }
  }
  uint32_t ram_base = core->getRamBase();
  uint32_t ram_size = core->getRamSize();
  if (loadImage) {
    unsigned num_phdrs = ehdr.e_phnum;
    if (num_phdrs == 0) {
      std::cerr << "No ELF program headers" << std::endl;
      std::exit(1);
    }
    for (unsigned i = 0; i < num_phdrs; i++) {
      GElf_Phdr phdr;
      if (gelf_getphdr(e, i, &phdr) == NULL) {
        std::cerr << "Reading ELF program header " << i << " failed: ";
        std::cerr << elf_errmsg(-1) << std::endl;
        std::exit(1);
      }
      if (phdr.p_filesz == 0) {
        continue;
      }
      if (phdr.p_offset > elfSector->getElfSize()) {
        std::cerr << "Invalid offet in ELF program header" << i << std::endl;
        std::exit(1);
      }
      if (core->isValidRamAddress(phdr.p_paddr) &&
          core->isValidRamAddress(phdr.p_paddr + phdr.p_memsz)) {
        core->writeMemory(phdr.p_paddr, &buf[phdr.p_offset], phdr.p_filesz);
      } else if (!hasValidVirtualAddress(phdr, *core)) {
        std::cerr << "Error data from ELF program header " << i;
        std::cerr << " does not fit in memory" << std::endl;
        std::exit(1);
      }
    }
  }

  std::auto_ptr<CoreSymbolInfo> CSI;
  readSymbols(e, ram_base, ram_base + ram_size, CSI);
  SymbolInfo &SI = sys.getSymbolInfo();
  SI.add(core, CSI);

  // Patch in syscall instruction at the syscall address.
  if (const ElfSymbol *syscallSym = SI.getGlobalSymbol(core, "_DoSyscall")) {
    if (!BM.setBreakpoint(*core, syscallSym->value, BreakpointType::Syscall)) {
      std::cout << "Warning: invalid _DoSyscall address "
      << std::hex << syscallSym->value << std::dec << "\n";
    }
  }
  // Patch in exception instruction at the exception address
  if (const ElfSymbol *doExceptionSym = SI.getGlobalSymbol(core, "_DoException")) {
    if (!BM.setBreakpoint(*core, doExceptionSym->value,
                          BreakpointType::Exception)) {
      std::cout << "Warning: invalid _DoException address "
      << std::hex << doExceptionSym->value << std::dec << "\n";
    }
  }
  if (useElfEntryPoint) {
    sys.schedule(core->getThread(0));
    sys.scheduleOther(*core->getParent()->getSSwitch(), 0);
    core->getThread(0).setPcFromAddress(entryPoint);
  }

  return 0;
}

int BootSequenceStepSchedule::execute(ExecutionState &state)
{
  SystemState &sys = state.sys;
  sys.schedule(core->getThread(0));
  sys.scheduleOther(*core->getParent()->getSSwitch(), 0);
  core->getThread(0).setPcFromAddress(address);
  return 0;
}

int BootSequenceStepRun::executeAux(ExecutionState &state)
{
  SystemState &sys = state.sys;
  BreakpointManager &BM = state.breakpointManager;
  SyscallHandler &SH = state.syscallHandler;
  SH.setDoneSyscallsRequired(numDoneSyscalls);
  StopReason stopReason = sys.run();
  while (stopReason.getType() == StopReason::BREAKPOINT) {
    Thread *thread = stopReason.getThread();
    Core &core = thread->getParent();
    uint32_t address = thread->getRealPc();
    int retval;
    switch (BM.getBreakpointType(core, address)) {
    case BreakpointType::Exception:
      SH.doException(*thread);
      return 1;
    case BreakpointType::Syscall:
      switch (SH.doSyscall(*thread, retval)) {
      case SyscallHandler::EXIT:
        return retval;
      case SyscallHandler::CONTINUE:
        thread->setPcFromAddress(thread->regs[Register::LR]);
        break;
      }
      break;
    case BreakpointType::Other:
      assert(0 && "Unexpected breakpoint type");
      break;
    }
    stopReason = sys.run();
  }
  switch (stopReason.getType()) {
    default: assert(0 && "Unexpected stop reason");
    case StopReason::NO_RUNNABLE_THREADS:
    case StopReason::TIMEOUT:
      return 1;
    case StopReason::EXIT:
      return stopReason.getStatus();
  }
}

int BootSequenceStepRun::execute(ExecutionState &state)
{
  int status = executeAux(state);
  state.breakpointManager.unsetBreakpoints();
  return status;
}

BootSequencer::BootSequencer(SystemState &s) :
  sys(s),
  syscallHandler(new SyscallHandler)
{
}

BootSequencer::~BootSequencer() {
  delete syscallHandler;
  for (BootSequenceStep *step : steps) {
    delete step;
  }
}

void BootSequencer::addElf(Core *c, const XEElfSector *elfSector) {
  steps.push_back(new BootSequenceStepElf(c, elfSector));
}

void BootSequencer::addSchedule(Core *c, uint32_t address) {
  steps.push_back(new BootSequenceStepSchedule(c, address));
}

void BootSequencer::addRun(unsigned numDoneSyscalls) {
  steps.push_back(new BootSequenceStepRun(numDoneSyscalls));
}

static std::vector<BootSequenceStep*>::iterator
getPenultimateRunStep(std::vector<BootSequenceStep*>::iterator begin,
                      std::vector<BootSequenceStep*>::iterator end)
{
  auto it = end;
  unsigned count = 0;
  while (it != begin) {
    --it;
    if ((*it)->getType() == BootSequenceStep::RUN
        && ++count == 2) {
      return it;
    }
  }
  return end;
}

void BootSequencer::setEntryPointToRom()
{
  std::vector<BootSequenceStep*> newSteps;
  for (BootSequenceStep *step : steps) {
    newSteps.push_back(step);
    if (step->getType() == BootSequenceStep::ELF) {
      BootSequenceStepElf *elfStep = static_cast<BootSequenceStepElf*>(step);
      elfStep->setUseElfEntryPoint(false);
      Core *core = elfStep->getCore();
      newSteps.push_back(new BootSequenceStepSchedule(core,
                                                      core->getRomBase()));
    }
  }
  std::swap(steps, newSteps);
}

void BootSequencer::eraseAllButLastImage()
{
  auto eraseTo = getPenultimateRunStep(steps.begin(), steps.end());
  if (eraseTo == steps.end())
    return;
  for (auto it = steps.begin(); it != eraseTo; ++it) {
    delete *it;
  }
  steps.erase(steps.begin(), eraseTo);
}

void BootSequencer::setLoadImages(bool value)
{
  for (BootSequenceStep *step : steps) {
    if (step->getType() == BootSequenceStep::ELF) {
      static_cast<BootSequenceStepElf*>(step)->setLoadImage(value);
    }
  }
}

static bool rangeOverlaps(uint32_t aBegin, uint32_t aEnd,
                          uint32_t bBegin, uint32_t bEnd)
{
  return aBegin < bEnd && bBegin < aEnd;
}

static bool
loadImage(ElfManager &elfManager, Core &core, void *dst, uint32_t src,
          uint32_t size)
{
  const LoadedElf *loadedElf = elfManager.getLoadedElf(core);
  if (!loadedElf)
    return false;
  Elf *e = loadedElf->getElf();
  const char *buf = loadedElf->getBuf();
  GElf_Ehdr ehdr;
  if (gelf_getehdr(e, &ehdr) == NULL)
    return false;
  unsigned num_phdrs = ehdr.e_phnum;
  if (num_phdrs == 0)
    return false;
  uint32_t srcBegin = src;
  uint32_t srcEnd = src + size;
  bool foundMatching = false;
  for (unsigned i = 0; i < num_phdrs; i++) {
    GElf_Phdr phdr;
    if (gelf_getphdr(e, i, &phdr) == NULL)
      return false;
    if (phdr.p_filesz == 0) {
      continue;
    }
    uint32_t segmentBegin = phdr.p_paddr;
    uint32_t segmentEnd = phdr.p_paddr + phdr.p_memsz;
    if (!rangeOverlaps(srcBegin, srcEnd, segmentBegin, segmentEnd))
      continue;
    foundMatching = true;
    uint32_t copyBegin = std::max(segmentBegin, srcBegin);
    uint32_t copyEnd = std::min(segmentEnd, srcEnd);
    uint32_t copySize = copyEnd - copyBegin;
    uint32_t offsetBegin = phdr.p_offset + copyBegin - segmentBegin;
    std::copy(&buf[offsetBegin], &buf[offsetBegin + copySize],
              reinterpret_cast<char*>(dst));
  }
  return foundMatching;
}

int BootSequencer::execute() {
  initializeElfHandling();
  ElfManager elfManager;
  auto loadImageCallback =
    [&](Core &core, void *dst, uint32_t src, uint32_t size) {
    return loadImage(elfManager, core, dst, src, size);
  };
  syscallHandler->setLoadImageCallback(loadImageCallback);
  ExecutionState executionState(sys, breakpointManager, *syscallHandler,
                                elfManager);
  for (BootSequenceStep *step : steps) {
    int status = step->execute(executionState);
    if (status != 0)
      return status;
  }
  return 0;
}

SyscallHandler* BootSequencer::getSyscallHandler() {
  return syscallHandler;
}

static inline void
addToCoreMap(std::map<std::pair<unsigned, unsigned>,Core*> &coreMap,
             ProcessorNode &node)
{
  unsigned jtagIndex = node.getJtagIndex();
  const std::vector<Core*> &cores = node.getCores();
  unsigned coreNum = 0;
  for (Core *core : cores) {
    coreMap.insert(std::make_pair(std::make_pair(jtagIndex, coreNum), core));
    coreNum++;
  }
}


static inline void
addToCoreMap(std::map<std::pair<unsigned, unsigned>,Core*> &coreMap,
             SystemState &system)
{
  for (Node *node : system.getNodes()) {
    if (!node->isProcessorNode())
      continue;
    addToCoreMap(coreMap, static_cast<ProcessorNode&>(*node));
  }
}

void BootSequencer::populateFromXE(XE &xe)
{
  std::map<std::pair<unsigned, unsigned>,Core*> coreMap;
  addToCoreMap(coreMap, sys);
  
  std::set<Core*> gotoSectors;
  std::set<Core*> callSectors;
  for (const XESector *sector : xe.getSectors()) {
    switch (sector->getType()) {
    case XESector::XE_SECTOR_ELF:
      {
        const XEElfSector *elfSector = static_cast<const XEElfSector*>(sector);
        unsigned jtagIndex = elfSector->getNode();
        unsigned coreNum = elfSector->getCore();
        Core *core = coreMap[std::make_pair(jtagIndex, coreNum)];
        if (!core) {
          std::cerr << "Error: cannot find node " << jtagIndex
          << ", core " << coreNum << std::endl;
          std::exit(1);
        }
        if (gotoSectors.count(core)) {
          // Shouldn't happen.
          break;
        }
        if (callSectors.count(core)) {
          addRun(callSectors.size());
          callSectors.clear();
        }
        addElf(core, elfSector);
      }
      break;
    case XESector::XE_SECTOR_CALL:
      {
        const XECallOrGotoSector *callSector =
        static_cast<const XECallOrGotoSector*>(sector);
        if (!gotoSectors.empty()) {
          // Shouldn't happen.
          break;
        }
        unsigned jtagIndex = callSector->getNode();
        unsigned coreNum = callSector->getCore();
        Core *core = coreMap[std::make_pair(jtagIndex, coreNum)];
        if (!core) {
          std::cerr << "Error: cannot find node " << jtagIndex
          << ", core " << coreNum << std::endl;
          std::exit(1);
        }
        if (!callSectors.insert(core).second) {
          addRun(callSectors.size());
          callSectors.clear();
          callSectors.insert(core);
        }
      }
      break;
    case XESector::XE_SECTOR_GOTO:
      {
        const XECallOrGotoSector *gotoSector =
        static_cast<const XECallOrGotoSector*>(sector);
        if (!callSectors.empty()) {
          // Handle calls.
          addRun(callSectors.size());
          callSectors.clear();
        }
        unsigned jtagIndex = gotoSector->getNode();
        unsigned coreNum = gotoSector->getCore();
        Core *core = coreMap[std::make_pair(jtagIndex, coreNum)];
        if (!core) {
          std::cerr << "Error: cannot find node " << jtagIndex
          << ", core " << coreNum << std::endl;
          std::exit(1);
        }
        if (!gotoSectors.insert(core).second) {
          // Shouldn't happen.
          break;
        }
      }
      break;
    }
  }
  if (!gotoSectors.empty()) {
    addRun(gotoSectors.size());
  } else if (!callSectors.empty()) {
    // Shouldn't happen.
    addRun(callSectors.size());
  }
}

void BootSequencer::adjustForSPIBoot()
{
  eraseAllButLastImage();
  setEntryPointToRom();
  setLoadImages(false);
  for (Node *node : sys.getNodes()) {
    if (!node->isProcessorNode())
      continue;
    for (Core *core : static_cast<ProcessorNode*>(node)->getCores()) {
      core->setBootModePins(1 << 2);
    }
  }
}

void BootSequencer::initializeElfHandling()
{
  static bool initialized = false;
  if (initialized)
    return;
  if (elf_version(EV_CURRENT) == EV_NONE) {
    std::cerr << "ELF library intialisation failed: "
              << elf_errmsg(-1) << std::endl;
    std::exit(1);
  }
  initialized = true;
}
