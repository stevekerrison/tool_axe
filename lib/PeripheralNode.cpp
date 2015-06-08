// Copyright (c) 2013, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#include "PeripheralNode.h"

using namespace axe;

PeripheralNode::PeripheralNode() : Node(Node::XS1_L, 1), Runnable()
{
  // Set the number of node bits to 0 so the switch accepts all messages.
  setNodeNumberBits(0);
}

void PeripheralNode::finalize()
{
  // The link is always enabled and in five wire mode.
  // TODO this part of the link configuration is hardcoded - we should disallow
  // changing it using control register writes.
  XLink &xlink = getXLink(0);
  xlink.setFiveWire(true);
  xlink.setEnabled(true);
}

ChanEndpoint *PeripheralNode::getOutgoingChanendDest(ResourceID ID, uint64_t *tokDelay)
{
  // All outgoing messages are routed over the link, regardless of the ID.
  XLink &xlink = getXLink(0);
  if (!xlink.isConnected())
    return nullptr;
  return xlink.getDestNode()->getOutgoingChanendDest(ID, tokDelay);
}

ChanEndpoint *PeripheralNode::getLocalChanendDest(ResourceID ID, uint64_t *tokDelay)
{
  return nullptr;
}

void PeripheralNode::run(ticks_t time)
{
    assert(0);//handleTokens(time);
}
