// Copyright (c) 2011, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#include <iomanip>
#include "SystemState.h"
#include "Node.h"
#include "Core.h"
#include "Trace.h"
#include "Stats.h"

using namespace Register;

SystemState::~SystemState()
{
  for (node_iterator it = nodes.begin(), e = nodes.end(); it != e; ++it) {
    delete *it;
  }
}

void SystemState::finalize()
{
  for (node_iterator it = nodes.begin(), e = nodes.end(); it != e; ++it) {
    (*it)->finalize();
  }
}

void SystemState::addNode(std::auto_ptr<Node> n)
{
  n->setParent(this);
  nodes.push_back(n.get());
  n.release();
}

void SystemState::
completeEvent(Thread &t, EventableResource &res, bool interrupt)
{
  if (interrupt) {
    t.regs[SSR] = t.sr.to_ulong();
    t.regs[SPC] = t.getParent().targetPc(t.pc);
    t.regs[SED] = t.regs[ED];
    t.ieble() = false;
    t.inint() = true;
    t.ink() = true;
  } else {
    t.inenb() = 0;
  }
  t.eeble() = false;
  // EventableResource::completeEvent sets the ED and PC.
  res.completeEvent();
  if (Tracer::get().getTracingEnabled()) {
    if (interrupt) {
      Tracer::get().interrupt(t, res, t.getParent().targetPc(t.pc),
                                      t.regs[SSR], t.regs[SPC], t.regs[SED],
                                      t.regs[ED]);
    } else {
      Tracer::get().event(t, res, t.getParent().targetPc(t.pc), t.regs[ED]);
    }
  }
}

int SystemState::run()
{
  try {
    while (!scheduler.empty()) {
      Runnable &runnable = scheduler.front();
      currentRunnable = &runnable;
      scheduler.pop();
      runnable.run(runnable.wakeUpTime);
    }
  } catch (ExitException &ee) {
    if (stats) {
      dump();
    }
    if (Stats::get().getStatsEnabled())
    {
      Stats::get().dump();
    }
    return ee.getStatus();
  }
  Tracer::get().noRunnableThreads(*this);
  return 1;
}

void SystemState::dump() {
  long totalCount = 0;
  ticks_t maxTime = 0;
  for (node_iterator nIt=node_begin(), nEnd=node_end(); nIt!=nEnd; ++nIt) {
    Node &node = **nIt;
    for (Node::core_iterator cIt=node.core_begin(), cEnd=node.core_end(); 
        cIt!=cEnd; ++cIt) {
      Core &core = **cIt;
      std::cout << "Core " << core.getCoreNumber() << std::endl;
      std::cout 
        << std::setw(8) << "Thread" << " "
        << std::setw(12) << "Time" << " "
        << std::setw(12) << "Insts" << " "
        << std::setw(12) << "Insts/cycle" << std::endl;
      for (int i=0; i<NUM_THREADS; i++) {
        Thread &thread = core.getThread(i);
        totalCount += thread.count;
        maxTime = maxTime > thread.time ? maxTime : thread.time;
        double ratio = (double) thread.count / (double) thread.time;
        std::cout 
          << std::setw(8) << i << " " 
          << std::setw(12) << thread.time << " "
          << std::setw(12) << thread.count << " " 
          << std::setw(12) << std::setprecision(2) << ratio << std::endl;
      }
    }
  }
  // Assume 10ns cycle (400Mhz clock)
  double seconds = (double) maxTime / 100000000.0;
  double opsPerSec = (double) totalCount / seconds;
  double gOpsPerSec = opsPerSec / 1000000.0;
  std::cout << std::endl;
  std::cout << "Total instructions executed:  " << totalCount << std::endl;
  std::cout << "Total cycles:                 " << maxTime << std::endl;
  std::cout << "Elapsed time (s):             " 
    << std::setprecision(2) << seconds << std::endl;
  std::cout << "Instructions per second:      "
    << std::setprecision(2) << opsPerSec
    << " (" << std::setprecision(2) << gOpsPerSec << " GIPS)" << std::endl;
  std::cout << std::endl;
}

