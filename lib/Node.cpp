// Copyright (c) 2011, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#include <algorithm>
#include "Node.h"
#include "BitManip.h"

#undef NDEBUG
#include <cassert>

using namespace axe;

XLink::XLink() :
  destNode(0),
  parent(0),
  destXLinkNum(0),
  enabled(false),
  fiveWire(false),
  waiting(false),
  network(0),
  direction(0),
  // TODO find out defaults.
  interTokenDelay(0),
  interSymbolDelay(0),
  outputCredit(0),
  wakeTime(-1),
  tokDelay(0),
  issuedCredit(false)
{
}

XLink *XLink::getDestXLink() const
{
  if (!destNode)
    return 0;
  return &destNode->getXLink(destXLinkNum);
}

bool XLink::isConnected() const
{
  if (!isEnabled())
    return false;
  const XLink *otherEnd = getDestXLink();
  if (!otherEnd || !otherEnd->isEnabled())
    return false;
  return isFiveWire() == otherEnd->isFiveWire();
}

void XLink::setTokDelay() {
  uint8_t bps = isFiveWire() ? 2 : 1;
  tokDelay = 8/bps * interSymbolDelay + interTokenDelay;
}

void XLink::setDirection(uint8_t value)
{
  parent->xLinkGroups[direction].xLinks.erase(this);
  direction = value;
  parent->xLinkGroups[direction].xLinks.insert(this);
}

void XLink::hello(ticks_t time, bool value) {
  if (value) {
    outputCredit = 0;
    getDestXLink()->receiveCtrlToken(time + tokDelay, CT_HELLO);
  }
}

void XLink::notifyDestClaimed(ticks_t time)
{
  // TODO
  assert(0);
}

void XLink::notifyDestCanAcceptTokens(ticks_t time, unsigned tokens)
{
  // TODO
  assert(0);
}

bool XLink::canAcceptToken()
{
  return outputCredit >= 8;
}

bool XLink::canAcceptTokens(unsigned tokens)
{
  // TODO
  assert(0);
}

void XLink::receiveDataToken(ticks_t time, uint8_t value)
{
  // TODO
  assert(0);
}

void XLink::receiveDataTokens(ticks_t time, uint8_t *values, unsigned num)
{
  // TODO
  assert(0);
}

bool XLink::openRoute()
{
  /*if (inPacket)
    return true;
  dest = getOwner().getParent().getChanendDest(destID, &tokDelay);
  if (!dest) {
    // TODO if dest in unset should give a link error exception.
    junkPacket = true;
  } else if (!dest->claim(this, junkPacket)) {
    return false;
  }
  inPacket = true;
  return true;
  // TODO
  assert(0);*/
  
}


void XLink::run(ticks_t time) {
  if (time < wakeTime) return;
  bool canpop = true;
  assert(!buf.empty());
  Token t = buf.front();
  uint8_t value = t.getValue();
  if (t.isControl()) {
    switch (value) {
    case CT_HELLO:
      {
        canpop = false; //We do this early for CREDITing
        issuedCredit = true;
        buf.pop_front();
        uint8_t credit = 0, bufrem = buf.remaining() * 8;
        if (bufrem >= 64) {
          credit = CT_CREDIT64;
        } else if (bufrem >= 16) {
          credit = CT_CREDIT16;
        } else if (bufrem >= 8) {
          credit = CT_CREDIT8;
        } else {
          //Oops, no credit for you!
        }
        if (credit) {
          getDestXLink()->receiveCtrlToken(time + tokDelay, credit);
        }
      }
      break;
    case CT_CREDIT64:
      outputCredit += 64;
      break;
    case CT_CREDIT16:
      outputCredit += 16;
      break;
    case CT_CREDIT8:
      outputCredit += 8;
      break;
    default:
      canpop = false;
      {
        //ChanEndpoint *nextDest = parent->getNextEndpoint(getSource()->getDestID());
        //nextDest->receiveCtrlToken(0, value);
        if (value == CT_END || value == CT_PAUSE) {
          //release(
        }
      }
      // TODO
      assert(0 && "Forward tokens & handle END/PAUSE");
      break;
    }
  } else {
    assert(0 && "Handle non-control tokens");
  }
  if (canpop) {
    buf.pop_front();
  }
  wakeTime = buf.empty() ? -1 : time + 1;
}

void XLink::receiveCtrlToken(ticks_t time, uint8_t value)
{
  assert(buf.remaining() > 0);
  buf.push_back(Token(value, true));
  assert(time > 0);
  wakeTime = time;
  return;
}

void XLink::release(ticks_t time)
{
  XLinkGroup *g = &parent->xLinkGroups[direction];
  if (g->queue.empty()) {
    source = 0;
    return;
  }
  source = queue.front();
  queue.pop();
  source->notifyDestClaimed(time);
}

/** XLinkGroup **/

ChanEndpoint *XLinkGroup::claim(ChanEndpoint *newSource, bool &junkPacket)
{
  assert(0 && "Implement XLinkGroup claiming");
  for (auto &xLink: xLinks) {
    if (!xLink->source) {
      xLink->source = newSource;
      // Change the ChanEndpoint to the actual link that will be used.
      return xLink;
    }
  }
  // No available links right now, so defer to the queue
  queue.push(newSource);
  return 0;
}

void XLinkGroup::notifyDestClaimed(ticks_t time)
{
  // TODO
  assert(0);
}

void XLinkGroup::notifyDestCanAcceptTokens(ticks_t time, unsigned tokens)
{
  // TODO
  assert(0);
}

bool XLinkGroup::canAcceptToken()
{
  // TODO
  assert(0);
}

bool XLinkGroup::canAcceptTokens(unsigned tokens)
{
  // TODO
  assert(0);
}

void XLinkGroup::receiveDataToken(ticks_t time, uint8_t value)
{
  // TODO
  assert(0);
}

void XLinkGroup::receiveDataTokens(ticks_t time, uint8_t *values, unsigned num)
{
  // TODO
  assert(0);
}

void XLinkGroup::receiveCtrlToken(ticks_t time, uint8_t value)
{
  // TODO
  assert(0);
}

Node::Node(Type t, unsigned numXLinks) :
  type(t),
  jtagIndex(0),
  nodeID(0),
  parent(0),
  sswitch(this),
  nodeNumberBits(16)
{
  xLinks.resize(numXLinks);
  for (auto &l : xLinks) {
    l.parent = this;
    xLinkGroups[l.direction].xLinks.insert(&l);
  }
}

Node::~Node()
{
}


void Node::setParent(SystemState *value) {
    parent = value;
    sswitch.setScheduler(&parent->getScheduler());
}

void Node::connectXLink(unsigned num, Node *destNode, unsigned destNum)
{
  xLinks[num].destNode = destNode;
  xLinks[num].destXLinkNum = destNum;
}

ChanEndpoint *Node::getXLinkForDirection(unsigned direction)
{
  if (xLinkGroups[direction].xLinks.size() > 0) {
    return &xLinkGroups[direction];
  }
  return 0;
}

void Node::setNodeNumberBits(unsigned value)
{
  nodeNumberBits = value;
  directions.resize(nodeNumberBits);
}

unsigned Node::getNodeNumberBits() const
{
  return nodeNumberBits;
}

unsigned Node::getNonNodeNumberBits() const
{
  return 16 - nodeNumberBits;
}

void Node::setNodeID(unsigned value)
{
  nodeID = value;
}

void Node::finalize()
{
  sswitch.initRegisters();
}

ChanEndpoint *Node::getIncomingChanendDest(ResourceID ID, uint64_t *tokDelay)
{
  Node *node = this;
  // Use Brent's algorithm to detect cycles.
  Node *tortoise = node;
  unsigned hops = 0;
  unsigned leapCount = 8;
  while (1) {
    unsigned destNode = ID.node() >> node->getNonNodeNumberBits();
    unsigned diff = destNode ^ node->getNodeID();
    if (diff == 0)
      break;
    // Lookup direction
    unsigned bit = 31 - countLeadingZeros(diff);
    unsigned direction = node->directions[bit];
    // Lookup Xlink.
    assert(0 && "FIX");
    XLink *xLink;// = node->getXLinkForDirection(direction);
    if (!xLink || !xLink->isConnected())
      return 0;
    node = xLink->destNode;
    ++hops;
    // Junk message if a cycle is detected.
    if (node == tortoise)
      return 0;
    if (hops == leapCount) {
      leapCount <<= 1;
      tortoise = node;
    }
    if (tokDelay) {
        unsigned bps = xLink->fiveWire ? 2 : 1;
        *tokDelay += 8/bps * xLink->interSymbolDelay + xLink->interTokenDelay;
    }
  }
  if (ID.isConfig() && ID.num() == RES_CONFIG_SSCTRL) {
    return &node->sswitch;
  }
  return node->getLocalChanendDest(ID, tokDelay);
}

ChanEndpoint *Node::getOutgoingChanendDest(ResourceID ID, uint64_t *tokDelay)
{
  return getIncomingChanendDest(ID, tokDelay);
}

ChanEndpoint *Node::getNextEndpoint(ResourceID ID)
{
  Node *node = this;
  unsigned destNode = ID.node() >> node->getNonNodeNumberBits();
  unsigned diff = destNode ^ node->getNodeID();
  if (diff == 0) {
    //Local delivery
    if (ID.isConfig() && ID.num() == RES_CONFIG_SSCTRL) {
      return &node->sswitch;
    }
    return node->getLocalChanendDest(ID);
  }
  // Lookup direction
  unsigned bit = 31 - countLeadingZeros(diff);
  unsigned direction = node->directions[bit];
  // Lookup Xlink(group).
  return node->getXLinkForDirection(direction);
}

bool Node::hasMatchingNodeID(ResourceID ID)
{
  return (ID.node() >> getNonNodeNumberBits()) == nodeID;
}
