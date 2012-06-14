// Copyright (c) 2011, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#include "Resource.h"
#include "ClockBlock.h"
#include "Core.h"
#include "PortNames.h"
#include <algorithm>

Port::Port() :
  EventableResource(RES_TYPE_PORT),
  clock(0),
  readyOutOf(0),
  loopback(0),
  tracer(0),
  pausedOut(0),
  pausedIn(0),
  pausedSync(0),
  readyOut(false),
  time(0),
  pinsInputValue() {}

std::string Port::getName() const
{
  std::string name;
  if (getPortName(getID(), name))
    return name;
  return "(Unknown port)";
}

Signal Port::getDataPortPinsValue() const
{
  assert(portType == DATAPORT);
  if (outputPort)
    return Signal(shiftReg & portWidthMask());
  return pinsInputValue;
}

uint32_t Port::getDataPortPinsValue(ticks_t time) const
{
  return getDataPortPinsValue().getValue(time);
}

Signal Port::getPinsValue() const {
  if (!outputPort) {
    return pinsInputValue;
  }
  return getPinsOutputValue();
}

bool Port::setCInUse(Thread &thread, bool val, ticks_t time)
{
  // TODO call update()?
  if (val) {
    data = 0;
    condition = COND_FULL;
    outputPort = false;
    buffered = false;
    transferRegValid = false;
    timeRegValid = false;
    holdTransferReg = false;
    validShiftRegEntries = 0;
    timestampReg = 0;
    shiftReg = 0;
    shiftRegEntries = 1;
    portShiftCount = 1;
    time = thread.time;
    portCounter = 0;
    readyMode = NOREADY;
    // TODO check.
    masterSlave = MASTER;
    portType = DATAPORT;
    transferWidth = getPortWidth();
    if (readyOutOf) {
      readyOutOf->detachReadyOut(*this);
      readyOutOf = 0;
    }
    if (clock->isFixedFrequency()) {
      nextEdge = clock->getEdgeIterator(time);
    }
    clearReadyOut(time);
  }
  eventableSetInUse(thread, val);
  return true;
}

bool Port::
setCondition(Thread &thread, Condition c, ticks_t time)
{
  update(time);
  updateOwner(thread);
  if (c == COND_AFTER)
    return false;
  condition = c;
  scheduleUpdateIfNeeded();
  return true;
}

bool Port::
setData(Thread &thread, uint32_t d, ticks_t time)
{
  update(time);
  updateOwner(thread);
  data = d & portWidthMask();
  scheduleUpdateIfNeeded();
  return true;
}

Signal Port::getPinsOutputValue() const
{
  if (portType == READYPORT) {
    if (readyOutOf)
      return Signal(readyOutOf->getReadyOutValue());
    return Signal(0);
  }
  if (portType == CLOCKPORT) {
    return clock->getValue();
  }
  assert(portType == DATAPORT);
  if (!outputPort)
    return Signal(0);
  return Signal(shiftReg & portWidthMask());
}

void Port::
outputValue(Signal value, ticks_t time)
{
  if (loopback)
    loopback->seePinsChange(value, time);
  if (outputPort)
    handlePinsChange(value, time);
}

void Port::
handlePinsChange(Signal value, ticks_t time)
{
  for (std::set<ClockBlock*>::iterator it = sourceOf.begin(),
       e = sourceOf.end(); it != e; ++it) {
    (*it)->setValue(value, time);
  }
  for (std::set<ClockBlock*>::iterator it = readyInOf.begin(),
       e = readyInOf.end(); it != e; ++it) {
    (*it)->setReadyInValue(value, time);
  }
  if (tracer)
    tracer->seePinsChange(value, time);
}

void Port::
handlePinsChange(uint32_t val, ticks_t time)
{
  handlePinsChange(Signal(val), time);
}

void Port::
handleReadyOutChange(bool value, ticks_t time)
{
  for (std::set<Port*>::iterator it = readyOutPorts.begin(),
       e = readyOutPorts.end(); it != e; ++it) {
    (*it)->outputValue(value, time);
  }
}

void Port::
seePinsChange(const Signal &value, ticks_t time)
{
  update(time);
  pinsInputValue = value;
  if (!isInUse() || outputPort)
    return;
  handlePinsChange(value, time);
  scheduleUpdateIfNeeded();
}

void Port::update(ticks_t newTime)
{
  assert(newTime >= time);
  if (!isInUse() || !clock->isFixedFrequency() || portType != DATAPORT) {
    time = newTime;
    return;
  }
  // Handle the first edge.
  if (nextEdge->time > newTime) {
    time = newTime;
    return;
  }
  seeEdge(nextEdge->type, nextEdge->time);
  ++nextEdge;
  if (nextEdge->time > newTime) {
    time = newTime;
    return;
  }
  const bool slowMode = false;
  // TODO handle these cases.
  if (slowMode || timeRegValid || useReadyOut()) {
    updateAux(newTime);
    return;
  }
  // Align to rising edge.
  if (nextEdge->type == Edge::RISING) {
    seeEdge(nextEdge->type, nextEdge->time);
    ++nextEdge;
    if (nextEdge->time > newTime) {
      time = newTime;
      return;
    }
  }
  if (outputPort) {
    if (!pausedIn) {
      // Optimisation to skip shifting out data which doesn't change the value on
      // the pins.
      unsigned numSignificantFallingEdges = validShiftRegEntries +
                                            portShiftCount - 1;
      if (pausedSync)
        numSignificantFallingEdges++;
      unsigned numSignificantEdges = 2 * numSignificantFallingEdges - 1;
      if ((nextEdge + (numSignificantEdges - 1))->time <= newTime) {
        while (numSignificantEdges != 0) {
          seeEdge(nextEdge->type, nextEdge->time);
          ++nextEdge;
          --numSignificantEdges;
        }
        unsigned skippedEdges = clock->getEdgeIterator(newTime) - nextEdge;
        skipEdges(skippedEdges / 2, (skippedEdges + 1) / 2);
        nextEdge += skippedEdges;
        time = newTime;
        return;
      }
    }
  } else {
    if (!pausedOut) {
      // Optimisation to skip shifting in data which will never seen.
      // TODO handle portShiftCount.
      unsigned numSignificantRisingEdges = shiftRegEntries * 2 - 1;
      unsigned numSignificantEdges = 2 * numSignificantRisingEdges;
      if ((nextEdge + (numSignificantEdges - 1))->time <= newTime) {
        unsigned numEdges = clock->getEdgeIterator(newTime) - nextEdge;
        unsigned skippedEdges = numEdges - numSignificantEdges;
        skipEdges((skippedEdges + 1) /2, skippedEdges /2);
        nextEdge += skippedEdges;
      }
    }
  }
  updateAux(newTime);
}

void Port::updateAux(ticks_t newTime)
{
  while (nextEdge->time <= newTime) {
    seeEdge(nextEdge->type, nextEdge->time);
    ++nextEdge;
  }
  time = newTime;
}

bool Port::
shouldRealignShiftRegister()
{
  assert(!outputPort);
  if (!isBuffered())
    return false;
  if (!pausedIn && !eventsPermitted())
    return false;
  if (holdTransferReg)
    return false;
  if (timeRegValid)
    return !useReadyOut() && portCounter == timeReg;
  return condition != COND_FULL &&
         valueMeetsCondition(getDataPortPinsValue(time));
}

uint32_t Port::
nextShiftRegOutputPort(uint32_t old)
{
  uint32_t repeatValue = old >> (getTransferWidth() - getPortWidth());
  uint32_t retval = old >> getPortWidth();
  retval |= repeatValue << (getTransferWidth() - getPortWidth());
  return retval;
}

void Port::
seeEdge(Edge::Type edgeType, ticks_t newTime)
{
  assert(newTime >= time);
  time = newTime;
  if (portType == READYPORT || portType == CLOCKPORT)
    return;
  assert(portType == DATAPORT);
  if (edgeType == Edge::FALLING) {
    portCounter++;
    if (outputPort) {
      if (timeRegValid && timeReg == portCounter) {
        // TODO support changing direction.
        assert(transferRegValid);
        timeRegValid = false;
        validShiftRegEntries = 0;
      }
      if (!useReadyIn() || clock->getReadyInValue(time) != 0) {
        uint32_t nextShiftReg = shiftReg;
        bool nextOutputPort = outputPort;
        if (validShiftRegEntries > 0) {
          validShiftRegEntries--;
        }
        if (validShiftRegEntries != 0) {
          nextShiftReg = nextShiftRegOutputPort(shiftReg);
        }
        if (validShiftRegEntries == 0) {
          if (pausedSync && !transferRegValid) {
            pausedSync->time = time;
            pausedSync->pc++;
            pausedSync->schedule();
            pausedSync = 0;
          }
          if (transferRegValid && !timeRegValid) {
            validShiftRegEntries = portShiftCount;
            portShiftCount = shiftRegEntries;
            nextShiftReg = transferReg;
            timestampReg = portCounter;
            transferRegValid = false;
            if (pausedOut) {
              pausedOut->time = time;
              pausedOut->schedule();
              pausedOut = 0;
            }
          } else if (pausedIn) {
            nextOutputPort = false;
            validShiftRegEntries = 0;
          }
        }
        bool pinsChange =
        (shiftReg ^ (nextOutputPort ? nextShiftReg : 0)) & portWidthMask();
        shiftReg = nextShiftReg;
        outputPort = nextOutputPort;
        if (pinsChange) {
          uint32_t newValue = getPinsOutputValue(time);
          outputValue(newValue, time);
        }
      }
    } else {
      if (useReadyOut() && timeRegValid && portCounter == timeReg) {
        timeRegValid = false;
        validShiftRegEntries = 0;
      }
    }
    updateReadyOut(time);
  } else {
    if (!outputPort &&
        (!useReadyOut() || (readyOut && !timeRegValid)) &&
        (!useReadyIn() || clock->getReadyInValue(time) != 0)) {
      uint32_t currentValue = getDataPortPinsValue(time);
      shiftReg >>= getPortWidth();
      shiftReg |= currentValue << (getTransferWidth() - getPortWidth());
      validShiftRegEntries++;
      if (shouldRealignShiftRegister()) {
        validShiftRegEntries = shiftRegEntries;
        transferRegValid = false;
        timeRegValid = false;
      }
      if (validShiftRegEntries == portShiftCount &&
          (!useReadyOut() || !transferRegValid ||
           timeRegValid || condition != COND_FULL)) {
        validShiftRegEntries = 0;
        if (!holdTransferReg) {
          portShiftCount = shiftRegEntries;
          transferReg = shiftReg;
          timestampReg = portCounter;
          transferRegValid = true;
          if (timeAndConditionMet()) {
            // TODO should the be conditional on pausedIn || EventPermitted()?
            timeRegValid = false;
            if (pausedIn) {
              pausedIn->time = time;
              pausedIn->schedule();
              pausedIn = 0;
            }
            if (eventsPermitted()) {
              event(time);
            }
            if (isBuffered()) {
              // TODO is this right?
              condition = COND_FULL;
            }
          }
        }
      }
    }
  }
}

void Port::skipEdges(unsigned numFalling, unsigned numRising)
{
  portCounter += numFalling;
  if (outputPort) {
    if (numFalling > validShiftRegEntries)
      validShiftRegEntries = 0;
    else
      validShiftRegEntries -= numFalling;
  } else {
    if (portShiftCount != shiftRegEntries) {
      if (validShiftRegEntries + numRising < portShiftCount) {
        validShiftRegEntries += numRising;
        return;
      }
      numRising -= portShiftCount;
      portShiftCount = shiftRegEntries;
    }
    validShiftRegEntries = (validShiftRegEntries + numRising) % shiftRegEntries;
  }
}

bool Port::seeOwnerEventEnable()
{
  assert(eventsPermitted());
  if (timeAndConditionMet()) {
    event(getOwner().time);
    return true;
  }
  scheduleUpdateIfNeeded();
  return false;
}

void Port::seeClockStart(ticks_t time)
{
  if (!isInUse())
    return;
  portCounter = 0;
  seeClockChange(time);
}

void Port::seeClockChange(ticks_t time)
{
  if (!isInUse())
    return;
  if (portType == CLOCKPORT) {
    outputValue(getPinsOutputValue(), time);
  } else if (portType == DATAPORT && clock->isFixedFrequency()) {
    nextEdge = clock->getEdgeIterator(time);
  }
  scheduleUpdateIfNeeded();
}

bool Port::valueMeetsCondition(uint32_t value) const
{
  switch (condition) {
  default: assert(0 && "Unexpected condition");
  case COND_FULL:
    return true;
  case COND_EQ:
    return data == value;
  case COND_NEQ:
    return data != value;
  }
}

bool Port::isValidPortShiftCount(uint32_t count) const
{
  return count >= getPortWidth() && count <= getTransferWidth() &&
         (count % getPortWidth()) == 0;
}

Resource::ResOpResult Port::
in(Thread &thread, ticks_t threadTime, uint32_t &value)
{
  update(threadTime);
  updateOwner(thread);
  // TODO is this right?
  if (portType != DATAPORT) {
    value = 0;
    return CONTINUE;
  }
  if (outputPort) {
    pausedIn = &thread;
    scheduleUpdateIfNeeded();
    return DESCHEDULE;
  }
  if (timeAndConditionMet()) {
    value = transferReg;
    if (validShiftRegEntries == portShiftCount) {
      portShiftCount = shiftRegEntries;
      transferReg = shiftReg;
      validShiftRegEntries = 0;
      // TODO is this right?
      timestampReg = portCounter;
    } else {
      transferRegValid = false;
    }
    holdTransferReg = false;
    return CONTINUE;
  }
  pausedIn = &thread;
  scheduleUpdateIfNeeded();
  return DESCHEDULE;
}

Resource::ResOpResult Port::
inpw(Thread &thread, uint32_t width, ticks_t threadTime, uint32_t &value)
{
  update(threadTime);
  updateOwner(thread);
  if (!isBuffered() || !isValidPortShiftCount(width)) {
    return ILLEGAL;
  }
  // TODO is this right?
  if (portType != DATAPORT) {
    value = 0;
    return CONTINUE;
  }
  if (outputPort) {
    pausedIn = &thread;
    scheduleUpdateIfNeeded();
    return DESCHEDULE;
  }
  if (timeAndConditionMet()) {
    value = transferReg;
    // TODO should validShiftRegEntries be reset?
    //validShiftRegEntries = 0;
    if (validShiftRegEntries == portShiftCount) {
      portShiftCount = shiftRegEntries;
      transferReg = shiftReg;
      // TODO is this right?
      timestampReg = portCounter;
    } else {
      transferRegValid = false;
    }
    holdTransferReg = false;
    return CONTINUE;
  }
  portShiftCount = width / getPortWidth();
  pausedIn = &thread;
  scheduleUpdateIfNeeded();
  return DESCHEDULE;
}

Resource::ResOpResult Port::
out(Thread &thread, uint32_t value, ticks_t threadTime)
{
  update(threadTime);
  updateOwner(thread);
  // TODO is this right?
  if (portType != DATAPORT) {
    return CONTINUE;
  }
  if (outputPort) {
    if (transferRegValid) {
      pausedOut = &thread;
      scheduleUpdateIfNeeded();
      return DESCHEDULE;
    }
  } else {
    // TODO probably wrong.
    validShiftRegEntries = 1;
  }
  transferRegValid = true;
  transferReg = value;
  outputPort = true;
  scheduleUpdateIfNeeded();
  return CONTINUE;
}

Resource::ResOpResult Port::
outpw(Thread &thread, uint32_t value, uint32_t width, ticks_t threadTime)
{
  update(threadTime);
  updateOwner(thread);
  if (!isBuffered() || !isValidPortShiftCount(width)) {
    return ILLEGAL;
  }
  // TODO is this right?
  if (portType != DATAPORT) {
    return CONTINUE;
  }
  if (outputPort) {
    if (transferRegValid) {
      pausedOut = &thread;
      scheduleUpdateIfNeeded();
      return DESCHEDULE;
    }
  } else {
    // TODO probably wrong.
    validShiftRegEntries = 1;
  }
  transferRegValid = true;
  portShiftCount = width / getPortWidth();
  transferReg = value;
  outputPort = true;
  scheduleUpdateIfNeeded();
  return CONTINUE;
}

Port::ResOpResult Port::
setpsc(Thread &thread, uint32_t width, ticks_t threadTime)
{
  update(threadTime);
  updateOwner(thread);
  if (!isBuffered() || !isValidPortShiftCount(width)) {
    return ILLEGAL;
  }
  // TODO is this right?
  if (portType != DATAPORT) {
    return CONTINUE;
  }
  portShiftCount = width / getPortWidth();
  scheduleUpdateIfNeeded();
  return CONTINUE;
}

Port::ResOpResult Port::
endin(Thread &thread, ticks_t threadTime, uint32_t &value)
{
  update(threadTime);
  updateOwner(thread);
  if (outputPort || !isBuffered()) {
    return ILLEGAL;
  }
  // TODO is this right?
  if (portType != DATAPORT) {
    value = 0;
    return CONTINUE;
  }
  unsigned entries = validShiftRegEntries;
  if (transferRegValid) {
    entries += shiftRegEntries;
    if (validShiftRegEntries != 0)
      portShiftCount = validShiftRegEntries;
  } else if (validShiftRegEntries != 0) {
    validShiftRegEntries = 0;
    portShiftCount = shiftRegEntries;
    transferReg = shiftReg;
    timestampReg = portCounter;
    transferRegValid = true;
  }
  value = entries * getPortWidth();
  scheduleUpdateIfNeeded();
  return CONTINUE;
}

Resource::ResOpResult Port::
sync(Thread &thread, ticks_t time)
{
  update(time);
  updateOwner(thread);
  if (portType != DATAPORT || !outputPort)
    return CONTINUE;
  pausedSync = &thread;
  scheduleUpdateIfNeeded();
  return DESCHEDULE;
}

uint32_t Port::
peek(Thread &thread, ticks_t threadTime)
{
  update(threadTime);
  updateOwner(thread);
  return getPinsValue().getValue(threadTime);
}

uint32_t Port::
getTimestamp(Thread &thread, ticks_t time)
{
  update(time);
  updateOwner(thread);
  return timestampReg;
}

Resource::ResOpResult
Port::setPortTime(Thread &thread, uint32_t value, ticks_t time)
{
  update(time);
  updateOwner(thread);
  if (portType != DATAPORT)
    return CONTINUE;
  if (outputPort) {
    if (transferRegValid) {
      pausedOut = &thread;
      scheduleUpdateIfNeeded();
      return DESCHEDULE;
    }
  }
  timeReg = value;
  timeRegValid = true;
  return CONTINUE;
}

void Port::clearPortTime(Thread &thread, ticks_t time)
{
  update(time);
  updateOwner(thread);
  timeRegValid = false;
}

void Port::clearBuf(Thread &thread, ticks_t time)
{
  update(time);
  updateOwner(thread);
  transferRegValid = false;
  holdTransferReg = false;
  validShiftRegEntries = 0;
  clearReadyOut(time);
}

bool Port::checkTransferWidth(uint32_t value)
{
  unsigned portWidth = getPortWidth();
  if (transferWidth == portWidth)
    return true;
  if (transferWidth < portWidth)
    return false;
  switch (value) {
  default: return false;
  // TODO check
  case 8:
  case 32:
    return true;
  }
}

void Port::setClkInitial(ClockBlock *c) {
  clock = c;
  clock->attachPort(this);
  portCounter = 0;
  seeClockChange(time);
}

void Port::setClk(Thread &thread, ClockBlock *c, ticks_t time)
{
  update(time);
  updateOwner(thread);
  clock->detachPort(this);
  clock = c;
  clock->attachPort(this);
  portCounter = 0;
  seeClockChange(time);
}

bool Port::setReady(Thread &thread, Port *p, ticks_t time)
{
  update(time);
  updateOwner(thread);
  if (getPortWidth() != 1)
    return false;
  if (readyOutOf) {
    p->detachReadyOut(*this);
  }
  readyOutOf = p;
  p->attachReadyOut(*this);
  outputValue(p->getReadyOutValue(), time);
  return true;
}

bool Port::setBuffered(Thread &thread, bool value, ticks_t time)
{
  update(time);
  updateOwner(thread);
  if (!value && (transferWidth != getPortWidth() || readyMode != NOREADY))
    return false;
  buffered = value;
  return true;
}

bool Port::setReadyMode(Thread &thread, ReadyMode mode, ticks_t time)
{
  update(time);
  updateOwner(thread);
  if (mode != NOREADY && !buffered)
    return false;
  readyMode = mode;
  scheduleUpdateIfNeeded();
  return true;
}

bool Port::setMasterSlave(Thread &thread, MasterSlave value, ticks_t time)
{
  update(time);
  updateOwner(thread);
  masterSlave = value;
  scheduleUpdateIfNeeded();
  return true;
}

bool Port::setPortType(Thread &thread, PortType type, ticks_t time)
{
  update(time);
  updateOwner(thread);
  if (portType == type)
    return true;
  Signal oldValue = getPinsOutputValue();
  bool oldOutputPort = outputPort;
  portType = type;
  if (type == DATAPORT) {
    outputPort = true;
  }
  Signal newValue = getPinsOutputValue();
  if (newValue != oldValue || !oldOutputPort)
    outputValue(newValue, time);
  scheduleUpdateIfNeeded();
  return true;
}

bool Port::setTransferWidth(Thread &thread, uint32_t value, ticks_t time)
{
  update(time);
  updateOwner(thread);
  if (!checkTransferWidth(value))
    return false;
  transferWidth = value;
  shiftRegEntries = transferWidth / getPortWidth();
  portShiftCount = shiftRegEntries;
  return true;
}

unsigned Port::fallingEdgesUntilTimeMet() const
{
  assert(timeRegValid);
  return (uint16_t)(timeReg - (portCounter + 1)) + 1;
}

void Port::scheduleUpdateIfNeededOutputPort()
{
  // If the next edge is a falling edge unconditionally schedule an update.
  // This simplifies the rest of the code since we can assume the next edge
  // is a rising edge.
  if (nextEdge->type == Edge::FALLING) {
    return scheduleUpdate(nextEdge->time);
  }
  if (readyOut != nextReadyOut()) {
    return scheduleUpdate((nextEdge + 1)->time);
  }
  bool readyInKnownZero = useReadyIn() && clock->getReadyInValue() == Signal(0);
  bool updateOnPinsChange = !sourceOf.empty() || loopback;
  if (!readyInKnownZero) {
    if (updateOnPinsChange && nextShiftRegOutputPort(shiftReg) != shiftReg)
      return scheduleUpdate((nextEdge + 1)->time);
    if (useReadyOut() && readyOut)
      return scheduleUpdate((nextEdge + 1)->time);
  }
  if (timeRegValid) {
    // Port counter is always updated on the falling edge.
    unsigned fallingEdges = fallingEdgesUntilTimeMet();
    unsigned edges = 2 * fallingEdges - 1;
    return scheduleUpdate((nextEdge + edges)->time);
  }
  if (!readyInKnownZero &&
      (pausedIn || pausedSync || transferRegValid)) {
    return scheduleUpdate((nextEdge + 1)->time);
  }
}

void Port::scheduleUpdateIfNeededInputPort()
{
  // If the next edge is a rising edge unconditionally schedule an update.
  // This simplifies the rest of the code since we can assume the next edge
  // is a falling edge.
  if (nextEdge->type == Edge::RISING) {
    return scheduleUpdate(nextEdge->time);
  }
  if (readyOut != nextReadyOut()) {
    return scheduleUpdate(nextEdge->time);
  }
  if (timeRegValid) {
    unsigned fallingEdges = fallingEdgesUntilTimeMet();
    unsigned edges = (fallingEdges - 1) * 2;
    if (!useReadyOut())
      edges++;
    return scheduleUpdate((nextEdge + edges)->time);
  } else {
    if (pausedOut) {
      return scheduleUpdate(nextEdge->time);
    }
    if ((!useReadyIn() || clock->getReadyInValue() != Signal(0)) &&
        (pausedIn || eventsPermitted() || (useReadyOut() && readyOut))) {
      Signal inputSignal = getDataPortPinsValue();
      if (inputSignal.isClock() ||
          valueMeetsCondition(inputSignal.getValue(time))) {
        return scheduleUpdate((nextEdge + 1)->time);
      }
    }
  }
}

void Port::scheduleUpdateIfNeeded()
{
  if (!isInUse() || !clock->isFixedFrequency() || portType != DATAPORT)
    return;
  const bool slowMode = false;
  if (slowMode) {
    if (pausedIn || eventsPermitted() || pausedOut || pausedSync ||
        !sourceOf.empty() || useReadyOut() || loopback) {
      return scheduleUpdate(nextEdge->time);
    }
  }
  if (outputPort)
    scheduleUpdateIfNeededOutputPort();
  else
    scheduleUpdateIfNeededInputPort();
}

bool Port::nextReadyOut()
{
  if (!useReadyOut())
    return 0;
  if (outputPort)
    return validShiftRegEntries != 0;
  if (timeRegValid)
    return portCounter == timeReg;
  return validShiftRegEntries != portShiftCount;
}

void Port::clearReadyOut(ticks_t time)
{
  if (readyOut == 0)
    return;
  readyOut = 0;
  handleReadyOutChange(0, time);
}

void Port::updateReadyOut(ticks_t time)
{
  bool newValue = nextReadyOut();
  if (newValue == readyOut)
    return;
  readyOut = newValue;
  handleReadyOutChange(newValue, time);
}

void Port::completeEvent()
{
  assert(transferRegValid);
  holdTransferReg = true;
  EventableResource::completeEvent();
}

bool Port::seeEventEnable(ticks_t time)
{
  // TODO what about other ports?
  assert(portType == DATAPORT);
  if (timeAndConditionMet()) {
    event(time);
    return true;
  }
  scheduleUpdateIfNeeded();
  return false;
}
