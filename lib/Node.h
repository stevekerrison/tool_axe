// Copyright (c) 2011, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#ifndef _Node_h_
#define _Node_h_

#include <stdint.h>
#include <vector>
#include <set>
#include <memory>
#include <queue>

#include "RunnableQueue.h"
#include "SSwitch.h"
#include "Resource.h"
#include "Token.h"
#include "SystemState.h"
#include "ChanEndpoint.h"
#include "ring_buffer.h"

namespace axe {

class Node;
class ProcessorNode;

class XLink : public Runnable, public ChanEndpoint {
  friend class Node;
  friend class SSwitch;
  friend class XLinkGroup;
  Node *destNode;
  Node *parent;
  unsigned destXLinkNum;
  bool enabled;
  bool fiveWire;
  bool waiting;
  uint8_t network;
  uint8_t direction;
  uint16_t interTokenDelay;
  uint16_t interSymbolDelay;
  uint8_t outputCredit;
  bool issuedCredit;
  ticks_t tokDelay;
  /// Input buffer.
  typedef ring_buffer<Token, XLINK_BUFFER_SIZE> XLinkBuffer;
  XLinkBuffer buf;
protected:
  bool openRoute();
  bool forward(ticks_t time, Token t);
public:
  XLink();
  Node *getDestNode() { return destNode; }
  XLink *getDestXLink() const;
  void setEnabled(bool value) { enabled = value; }
  bool isEnabled() const { return enabled; }
  void hello(ticks_t time, bool value);
  void setFiveWire(bool value) { fiveWire = value; }
  bool isFiveWire() const { return fiveWire; }
  void setNetwork(uint8_t value) { network = value; }
  void setTokDelay();
  bool hasIssuedCredit() const { return issuedCredit; }
  bool hasCredit() const { return outputCredit >= 8; }
  uint8_t getNetwork() const { return network; }
  void setDirection(uint8_t value);
  uint8_t getDirection() const { return direction; }
  void setInterTokenDelay(uint16_t value) { interTokenDelay = value + 2; }
  uint16_t getInterTokenDelay() const { return interTokenDelay; }
  void setInterSymbolDelay(uint16_t value) { interSymbolDelay = value + 1; }
  uint16_t getInterSymbolDelay() const { return interSymbolDelay; }
  bool isConnected() const;
  void run(ticks_t time);
  
    /* ChanEndpoint overrides */
  void release(ticks_t time);
  void notifyDestCanAcceptTokens(ticks_t time, unsigned tokens) override;
  bool canAcceptToken() override;
  bool canAcceptTokens(unsigned tokens) override;
  void receiveDataToken(ticks_t time, uint8_t value) override;
  void receiveDataTokens(ticks_t time, uint8_t *values, unsigned num) override;
  void receiveCtrlToken(ticks_t time, uint8_t value) override;
  void notifyDestClaimed(ticks_t time) override;
};

class XLinkGroup : public ChanEndpoint {
  friend class Node;
  friend class XLink;
  std::set<XLink *> xLinks;
  
public:
    /* ChanEndpoint overrides */
  ChanEndpoint *claim(ChanEndpoint *Source, bool &junkPacket);

  void notifyDestCanAcceptTokens(ticks_t time, unsigned tokens) override;
  bool canAcceptToken() override;
  bool canAcceptTokens(unsigned tokens) override;
  void receiveDataToken(ticks_t time, uint8_t value) override;
  void receiveDataTokens(ticks_t time, uint8_t *values, unsigned num) override;
  void receiveCtrlToken(ticks_t time, uint8_t value) override;
  void notifyDestClaimed(ticks_t time) override;
};

class Node {
public:
  enum Type {
    XS1_L,
    XS1_G
  };
  Type type;
  std::vector<XLink> xLinks;
  XLinkGroup xLinkGroups[8];
  std::vector<uint8_t> directions;
  unsigned jtagIndex;
  unsigned nodeID;
  SystemState *parent;
  SSwitch sswitch;
  unsigned nodeNumberBits;
  ChanEndpoint *getXLinkForDirection(unsigned direction);
protected:
  void setNodeNumberBits(unsigned value);
public:
  Node(Type type, unsigned numXLinks);
  Node(const Node &) = delete;
  virtual ~Node();
  virtual bool isProcessorNode() { return false; }
  unsigned getNodeNumberBits() const;
  unsigned getNonNodeNumberBits() const;
  virtual void finalize();
  void setJtagIndex(unsigned value) { jtagIndex = value; }
  unsigned getJtagIndex() const { return jtagIndex; }
  void setParent(SystemState *value);
  const SystemState *getParent() const { return parent; }
  SystemState *getParent() { return parent; }
  virtual void setNodeID(unsigned value);
  uint32_t getNodeID() const { return nodeID; }
  bool hasMatchingNodeID(ResourceID ID);
  SSwitch *getSSwitch() { return &sswitch; }
  unsigned getNumXLinks() const { return xLinks.size(); }
  XLink &getXLink(unsigned num) { return xLinks[num]; }
  const XLink &getXLink(unsigned num) const { return xLinks[num]; }
  void connectXLink(unsigned num, Node *destNode, unsigned destNum);
  /// Find the destination of a packet with the specified resource ID that was
  /// received on a link from another node.
  ChanEndpoint *getIncomingChanendDest(ResourceID ID, uint64_t *tokDelay = 0);
  /// Find the destination of a packet sent to the specified resource ID from
  /// this node.
  virtual ChanEndpoint *getOutgoingChanendDest(ResourceID ID, uint64_t *tokDelay = 0);
  virtual ChanEndpoint *getLocalChanendDest(ResourceID ID, uint64_t *tokDelay = 0) = 0;
  virtual ChanEndpoint *getNextEndpoint(ResourceID ID);
  uint8_t getDirection(unsigned num) const { return directions[num]; }
  void setDirection(unsigned num, uint8_t value) { directions[num] = value; }
  Type getType() const { return type; }
};
  
} // End axe namespace

#endif // _Node_h_
