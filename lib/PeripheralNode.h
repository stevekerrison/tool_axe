// Copyright (c) 2013, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#ifndef _PeripheralNode_h
#define _PeripheralNode_h

#include "Node.h"

namespace axe {

class PeripheralNode : public Runnable, public Node {
public:
  PeripheralNode();
  void finalize() override;
  ChanEndpoint *getOutgoingChanendDest(ResourceID ID, uint64_t *tokDelay = 0) override;
  ChanEndpoint *getLocalChanendDest(ResourceID ID, uint64_t *tokDelay = 0) override;
  ChanEndpoint *getNextEndpoint(ResourceID ID) override;
  void run(ticks_t time) override;
};

}  // End axe namespace

#endif // _PeripheralNode_h
