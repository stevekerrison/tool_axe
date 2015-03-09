// Copyright (c) 2011, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#include "Chanend.h"
#include "Core.h"
#include <algorithm>

using namespace axe;

bool Chanend::canAcceptToken()
{
  return !buf.full();
}

bool Chanend::canAcceptTokens(unsigned tokens)
{
  return buf.remaining() >= tokens;
}

void Chanend::receiveDataToken(ticks_t time, uint8_t value)
{
  buf.push_back(Token(value));
  update(time);
}

void Chanend::receiveDataTokens(ticks_t time, uint8_t *values, unsigned num)
{
  for (unsigned i = 0; i < num; i++) {
    buf.push_back(Token(values[i]));
  }
  update(time);
}

void Chanend::receiveCtrlToken(ticks_t time, uint8_t value)
{
  switch (value) {
  case CT_END:
    buf.push_back(Token(value, true));
    release(time);
    update(time);
    break;
  case CT_PAUSE:
    release(time);
    break;
  default:
    buf.push_back(Token(value, true));
    update(time);
    break;
  }
}

void Chanend::notifyDestClaimed(ticks_t time)
{
  if (pausedOut) {
    pausedOut->time = time;
    pausedOut->schedule();
    pausedOut = 0;
  }
}

// TODO can this be merged with the above function.
void Chanend::notifyDestCanAcceptTokens(ticks_t time, unsigned tokens)
{
  if (pausedOut) {
    pausedOut->time = time;
    pausedOut->schedule();
    pausedOut = 0;
  }
}

bool Chanend::openRoute()
{
  if (inPacket)
    return true;
  tokDelay = {};
  dest = getOwner().getParent().getChanendDest(destID, &tokDelay);
  if (!dest) {
    // TODO if dest in unset should give a link error exception.
    junkPacket = true;
  } else if (!dest->claim(this, junkPacket)) {
    return false;
  }
  inPacket = true;
  return true;
}

bool Chanend::setData(Thread &thread, uint32_t value, ticks_t time)
{
  if (inPacket)
    return false;
  ResourceID id(value);
  if (id.type() != RES_TYPE_CHANEND &&
      id.type() != RES_TYPE_CONFIG)
    return false;
  destID = value;
  return true;
}

bool Chanend::getData(Thread &thread, uint32_t &result, ticks_t time)
{
  result = destID;
  return true;
}

/*
 * Update the remote receive time based on a simplistic model of
 * cut-through routing.
 */
void Chanend::routeDelay(uint64_t time, uint8_t n_tokens) {
  uint64_t rtime = time + tokDelay.delay + ((n_tokens - 1) * (tokDelay.trate));
  if (rtime <= tokDelay.rrec) {
    tokDelay.rrec = tokDelay.rrec + (n_tokens * tokDelay.trate);
  } else {
    tokDelay.rrec = rtime;
  }
  switch (tokDelay.header_sent) {
  case RES_CH_SENT_NO:
    //Add on a header!
    /*tokDelay.rrec += (3 * tokDelay.trate) +
        (8 * tokDelay.hops) + (n_tokens * tokDelay.trate);*/
    /*tokDelay.rrec += tokDelay.delay +
        (3 * tokDelay.trate) + ((n_tokens-1) * tokDelay.trate);*/
    tokDelay.rrec += 3 * tokDelay.trate;
    tokDelay.rrec += (tokDelay.hops > 2 ? 16 : 8) * tokDelay.hops;
    /* Fallthrough */
  case RES_CH_SENT_LOCAL:
    //tokDelay.rrec += tokDelay.delay;
    tokDelay.header_sent = RES_CH_SENT_YES;
    break;    
  case RES_CH_SENT_YES:
  default: //WAT
    //tokDelay.rrec += (n_tokens * tokDelay.trate) + (8 * tokDelay.hops);
    //tokDelay.rrec += tokDelay.delay + ((n_tokens-1) * tokDelay.trate);
    //tokDelay.rrec += (n_tokens) * tokDelay.trate;
    break;
  }
  return;
}

Resource::ResOpResult Chanend::
outt(Thread &thread, uint8_t value, ticks_t time)
{
  if (!openRoute()) {
    pausedOut = &thread;
    return DESCHEDULE;
  }
  if (junkPacket)
    return CONTINUE;
  if (!dest->canAcceptToken()) {
    pausedOut = &thread;
    return DESCHEDULE;
  }
  routeDelay((uint64_t) time, 1);
  dest->receiveDataToken(tokDelay.rrec, value);
  return CONTINUE;
}

Resource::ResOpResult Chanend::
out(Thread &thread, uint32_t value, ticks_t time)
{
  if (!openRoute()) {
    pausedOut = &thread;
    return DESCHEDULE;
  }
  if (junkPacket)
    return CONTINUE;
  if (!dest->canAcceptTokens(4)) {
    pausedOut = &thread;
    return DESCHEDULE;
  }
  // Channels are big endian
  uint8_t tokens[4] = {
    static_cast<uint8_t>(value >> 24),
    static_cast<uint8_t>(value >> 16),
    static_cast<uint8_t>(value >> 8),
    static_cast<uint8_t>(value)
  };
  routeDelay((uint64_t) time, 4);
  dest->receiveDataTokens(tokDelay.rrec, tokens, 4);
  return CONTINUE;
}

Resource::ResOpResult Chanend::
outct(Thread &thread, uint8_t value, ticks_t time)
{
  if (!openRoute()) {
    pausedOut = &thread;
    return DESCHEDULE;
  }
  if (junkPacket) {
    if (value == CT_END || value == CT_PAUSE) {
      inPacket = false;
      junkPacket = false;
    }
    return CONTINUE;
  }
  if (!dest->canAcceptToken()) {
    pausedOut = &thread;
    return DESCHEDULE;
  }
  routeDelay((uint64_t) time, 1);
  dest->receiveCtrlToken(tokDelay.rrec, value);
  if (value == CT_END || value == CT_PAUSE) {
    inPacket = false;
    dest = 0;
  }
  return CONTINUE;
}

bool Chanend::
testct(Thread &thread, ticks_t time, bool &isCt)
{
  updateOwner(thread);
  if (buf.empty()) {
    setPausedIn(thread, false);
    return false;
  }
  isCt = buf.front().isControl();
  return true;
}

bool Chanend::
testwct(Thread &thread, ticks_t time, unsigned &position)
{
  updateOwner(thread);
  position = 0;
  unsigned numTokens = std::min(buf.size(), 4U);
  for (unsigned i = 0; i < numTokens; i++) {
    if (buf[i].isControl()) {
      position = i + 1;
      return true;
    }
  }
  if (buf.size() < 4) {
    setPausedIn(thread, true);
    return false;
  }
  return true;
}

uint8_t Chanend::poptoken(ticks_t time)
{
  assert(!buf.empty() && "poptoken on empty buf");
  uint8_t value = buf.front().getValue();
  buf.pop_front();
  if (getSource()) {
    getSource()->notifyDestCanAcceptTokens(time, buf.remaining());
  }
  return value;
}

void Chanend::setPausedIn(Thread &t, bool wordInput)
{
  pausedIn = &t;
  waitForWord = wordInput;
}

Resource::ResOpResult Chanend::
intoken(Thread &thread, ticks_t time, uint32_t &val)
{
  bool isCt;
  if (!testct(thread, time, isCt)) {
    return DESCHEDULE;
  }
  if (isCt)
    return ILLEGAL;
  val = poptoken(time);
  return CONTINUE;
}

Resource::ResOpResult Chanend::
inct(Thread &thread, ticks_t time, uint32_t &val)
{
  bool isCt;
  if (!testct(thread, time, isCt)) {
    return DESCHEDULE;
  }
  if (!isCt)
    return ILLEGAL;
  val = poptoken(time);
  return CONTINUE;
}

Resource::ResOpResult Chanend::
chkct(Thread &thread, ticks_t time, uint32_t value)
{
  bool isCt;
  if (!testct(thread, time, isCt)) {
    return DESCHEDULE;
  }
  if (!isCt || buf.front().getValue() != value)
    return ILLEGAL;
  (void)poptoken(time);
  return CONTINUE;
}

Resource::ResOpResult Chanend::
in(Thread &thread, ticks_t time, uint32_t &value)
{
  unsigned Position;
  if ((!testwct(thread, time, Position)) )// || (pausedIn && thread.time < pausedIn->time) )
    return DESCHEDULE;
  if (Position != 0)
    return ILLEGAL;
  value = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
  buf.pop_front(4);
  if (getSource()) {
    getSource()->notifyDestCanAcceptTokens(time, buf.remaining());
  }
  return CONTINUE;
}

void Chanend::update(ticks_t time)
{
  assert(!buf.empty());
  if (eventsPermitted()) {
    event(time);
    return;
  }
  /* If the token has arrived in the future... um, schedule! */
  if (getOwner().time < time) {
    setPausedIn(getOwner(), true);
    pausedIn->time = time;
    pausedIn->schedule();
    pausedIn = 0;
    return;
  }
  if (!pausedIn)
    return;
  if (waitForWord && buf.size() < 4)
    return;
  pausedIn->time = time;
  pausedIn->schedule();
  pausedIn = 0;
}

void Chanend::run(ticks_t time)
{
  assert(0 && "Shouldn't get here");
}

bool Chanend::seeEventEnable(ticks_t time)
{
  if (buf.empty())
    return false;
  event(time);
  return true;
}
