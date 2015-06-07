// Copyright (c) 2011, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#include "SSwitch.h"
#include "Resource.h"
#include "ProcessorNode.h"
#include "SystemState.h"
#include "Tracer.h"
#undef NDEBUG
#include <cassert>

using namespace axe;

SSwitch::SSwitch(Node *p) :
  parent(p),
  regs(static_cast<ProcessorNode*>(p)),
  recievedTokens(0),
  junkIncomingTokens(0),
  sendingResponse(false),
  sentTokens(0),
  responseLength(0),
  scheduler(0)
{
  setJunkIncoming(false);
  wakeUpTime = 0;
}

static uint16_t read16_be(const Token *p)
{
  return (p[0].getValue() << 8) | p[1].getValue();
}

static uint32_t read32_be(const Token *p)
{
  return (p[0].getValue() << 24) | (p[1].getValue() << 16) |
         (p[2].getValue() << 8) | p[3].getValue();
}

static void write32_be(Token *p, uint32_t value)
{
  p[0] = Token(value >> 24);
  p[1] = Token((value >> 16) & 0xff);
  p[2] = Token((value >> 8) & 0xff);
  p[3] = Token(value & 0xff);
}

static bool containsControlToken(const Token *p, unsigned size)
{
  for (unsigned i = 0; i < size; ++i) {
    if (p[i].isControl())
      return true;
  }
  return false;
}

bool SSwitch::parseRequest(ticks_t time, Request &request) const
{
  if (recievedTokens == 0)
    return false;
  if (!buf[0].isControl())
    return false;
  unsigned expectedLength;
  switch (buf[0].getValue()) {
  default:
    return false;
  case CT_READC:
    expectedLength = readRequestLength;
    request.write = false;
    break;
  case CT_WRITEC:
    expectedLength = writeRequestLength;
    request.write = true;
    break;
  }
  if (recievedTokens != expectedLength)
    return false;
  if (containsControlToken(&buf[1], expectedLength - 1))
    return false;
  request.returnNode = read16_be(&buf[1]);
  request.returnNum = buf[3].getValue();
  request.regNum = read16_be(&buf[4]);
  if (request.write) {
    request.data = read32_be(&buf[6]);
  } else {
    request.data = 0;
  }
  return true;
}

void SSwitch::handleRequest(ticks_t time, const Request &request)
{
  bool ack;
  uint32_t value = 0;
  ResourceID destID = ResourceID::chanendID(request.returnNum,
                                            request.returnNode);
  Tracer *tracer = parent->getParent()->getTracer();
  if (request.write) {
    ack = regs.write(time, request.regNum, request.data);
    if (tracer) {
      tracer->SSwitchWrite(*parent, destID, request.regNum, request.data);
      if (ack)
        tracer->SSwitchAck(*parent, destID);
      else
        tracer->SSwitchNack(*parent, destID);
    }
  } else {
    ack = regs.read(request.regNum, value);
    if (tracer) {
      tracer->SSwitchRead(*parent, destID, request.regNum);
      if (ack)
        tracer->SSwitchAck(*parent, value, destID);
      else
        tracer->SSwitchNack(*parent, destID);
    }
  }
  ChanEndpoint *dest = parent->getNextEndpoint(destID);
  if (!dest)
    return;
  bool junkPacket = false;
  if (!dest->claim(this, junkPacket)) {
    assert(0 && "TODO");
  }
  if (junkPacket)
    return;
  sendingResponse = true;
  sentTokens = 0;
  responseLength = 0;
  if (ack) {
    buf[responseLength++] = Token(CT_ACK, true);
    if (!request.write) {
      write32_be(&buf[responseLength], value);
      responseLength += 4;
    }
  } else {
    buf[responseLength++] = Token(CT_NACK, true);
  }
  buf[responseLength++] = Token(CT_END, true);
  if (!dest->canAcceptTokens(responseLength)) {
    assert(0 && "TODO");
  }
  for (unsigned i = 0; i < responseLength; i++) {
    if (buf[i].isControl()) {
      dest->receiveCtrlToken(time, buf[i].getValue());
    } else {
      dest->receiveDataToken(time, buf[i].getValue());
    }
  }
  sendingResponse = false;
}

void SSwitch::notifyDestClaimed(ticks_t time)
{
  // TODO
  assert(0);
}

void SSwitch::notifyDestCanAcceptTokens(ticks_t time, unsigned tokens)
{
  // TODO
  assert(0);
}

bool SSwitch::canAcceptToken()
{
  return !sendingResponse;
}

bool SSwitch::canAcceptTokens(unsigned tokens)
{
  return !sendingResponse;
}

void SSwitch::receiveDataToken(ticks_t time, uint8_t value)
{
  if (junkIncomingTokens) {
    return;
  }
  if (recievedTokens == writeRequestLength) {
    junkIncomingTokens = true;
    return;
  }
  buf[recievedTokens++] = Token(value);
}

void SSwitch::receiveDataTokens(ticks_t time, uint8_t *values, unsigned num)
{
  if (junkIncomingTokens) {
    return;
  }
  if (recievedTokens + num > writeRequestLength) {
    junkIncomingTokens = true;
    return;
  }
  for (unsigned i = 0; i < num; i++) {
    buf[recievedTokens++] = Token(values[i]);
  }
}

void SSwitch::receiveCtrlToken(ticks_t time, uint8_t value)
{
  switch (value) {
  case CT_END:
    Request request;
    if (!junkIncomingTokens && parseRequest(time, request)) {
      handleRequest(time, request);
    }
    recievedTokens = 0;
    junkIncomingTokens = false;
    release(time);
    return;
  case CT_PAUSE:
    release(time);
    return;
  }
  if (junkIncomingTokens) {
    return;
  }
  if (recievedTokens == writeRequestLength) {
    junkIncomingTokens = true;
    return;
  }
  buf[recievedTokens++] = Token(value, true);
}

void SSwitch::handleTokens(ticks_t time)
{
    size_t lim = parent->getNumXLinks();
    for (int i = 0; i < lim; i += 1) {
        parent->getXLink(i).run(time);
    }
}

void SSwitch::run(ticks_t time)
{
    handleTokens(time);
    scheduler->push(*this, time + 1);
}
