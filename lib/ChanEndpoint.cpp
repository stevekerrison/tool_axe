// Copyright (c) 2011, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#include "ChanEndpoint.h"

using namespace axe;

ChanEndpoint::ChanEndpoint() :
  junkIncoming(true),
  source(0)
{
}

ChanEndpoint *ChanEndpoint::claim(ChanEndpoint *newSource, bool &junkPacket)
{
  if (junkIncoming) {
    junkPacket = true;
    return this;
  }
  // Check if the route is already open.
  if (source == newSource) {
    return this;
  }
  // Check if we are already in the middle of a packet.
  if (source) {
    queue.push(newSource);
    return 0;
  }
  // Claim the channel
  source = newSource;
  return this;
}

void ChanEndpoint::release(ticks_t time)
{
  if (queue.empty()) {
    source = 0;
    return;
  }
  source = queue.front();
  queue.pop();
  source->notifyDestClaimed(time);
}
