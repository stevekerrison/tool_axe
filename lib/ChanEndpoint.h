// Copyright (c) 2011, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#ifndef _ChanEndpoint_h_
#define _ChanEndpoint_h_

#include <queue>
#include <vector>
#include "Config.h"

namespace axe {

class ChanEndpoint {
private:
  /// Should incoming packets be junked?
  bool junkIncoming;
protected:
  /// Chanends blocked on the route to this channel end becoming free.
  std::queue<ChanEndpoint *> queue;
  uint32_t destID;
  /// The destination channel end. Only valid when in the the middle of a
  /// packet.
  ChanEndpoint *dest;
  /// The source of the current packet, 0 if not receiving a packet.
  ChanEndpoint *source;
  /// Are we in the middle of sending a packet?
  bool inPacket;
  /// Should be current packet be junked?
  bool junkPacket;
  
  /// Where the n
  void setJunkIncoming(bool value) { junkIncoming = value; }
  ChanEndpoint *getSource() const { return source; }  

  /// Try and open a route for a packet. If a route cannot be opened the chanend
  /// is registered with the destination and notifyDestClaimed() will be called
  /// when the route becomes available.
  bool openRoute();
  ChanEndpoint();
  ~ChanEndpoint() = default;

  /// Record the route delay per token, assuming tok/sym delays do not change
  uint64_t delay;
public:
  uint32_t getDestID() { return destID; }
  /// Give notification that a route to the destination has been opened.
  virtual void notifyDestClaimed(ticks_t time) = 0;

  /// Give notification that the destination can accept  in the buffer has
  /// become available.
  virtual void notifyDestCanAcceptTokens(ticks_t time, unsigned tokens) = 0;

  /// Called when trying to open a route to this channel end. If the route
  /// cannot be opened immediately then the source chanend is added to a queue.
  /// The source will be notified when the route becomes available with
  /// notifyDestClaimed().
  /// \param junkPacket Set to false if the packet should be junked, otherwise
  ///                   left unchanged.
  /// \return Whether a route was succesfully opened.
  virtual ChanEndpoint *claim(ChanEndpoint *Source, bool &junkPacket);
  
    /// End the current packet being sent to the channel end.
  virtual void release(ticks_t time);
  
  virtual bool canAcceptToken() = 0;
  virtual bool canAcceptTokens(unsigned tokens) = 0;

  /// Recieve data token. The caller must check sufficient room is available
  /// using canAcceptToken().
  virtual void receiveDataToken(ticks_t time, uint8_t value) = 0;

  /// Recieve data tokens. The caller must check sufficient room is available
  /// using canAcceptTokens().
  virtual void receiveDataTokens(ticks_t time, uint8_t *values, unsigned num) = 0;

  /// Recieve control token. The caller must check sufficient room is available
  /// using canAcceptTokens().
  virtual void receiveCtrlToken(ticks_t time, uint8_t value) = 0;
};
  
} // End axe namespace

#endif // _ChanEndpoint_h_
