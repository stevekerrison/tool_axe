// Copyright (c) 2011 Richard Osbourne, 2012 Steve Kerrison, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#include "Stats.h"
#include "SystemState.h"
#include "Node.h"
#include "Core.h"
#include "Resource.h"
#include "Exceptions.h"
#include <iomanip>
#include <sstream>
#include <cstring>


Stats Stats::instance;

void Stats::updateStats(const ThreadState &t, const char *name) {
  std::string s(name);
  int cid = t.getParent().getCoreID(),
    tid = t.getID();
  std::map<std::string, long long*>::iterator iter = istats.find(s);
  if (iter == istats.end())
  {
    long long *x = new long long[cores * NUM_THREADS]();
    istats.insert(std::pair<std::string, long long*>(name,x));
    iter = istats.find(s);
  }
  long long *ts = iter->second;
  ts[(NUM_THREADS * cid) + tid] += 1;
  return;
}

void Stats::dump() {
  std::cout << "Dumping stats...\n";
  return;
}

void Stats::initStats(const int cores) {
  Stats::cores = cores;
}

