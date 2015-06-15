// Copyright (c) 2011, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#ifndef _Token_h_
#define _Token_h_

#include "Config.h"
#include <stdint.h>

#undef NDEBUG
#include <cassert>

namespace axe {

enum ControlTokenValue {
  CT_HDR = 0, // TODO: This was a complete guess
  CT_END = 1,
  CT_PAUSE = 2,
  CT_ACK = 3,
  CT_NACK = 4,
  CT_WRITEC = 0xc0,
  CT_READC = 0xc1,
  CT_CREDIT8 = 0xe0,
  CT_CREDIT64 = 0xe1,
  CT_CREDIT16 = 0xe4,
  CT_HELLO = 0xe6
};

class Token {
private:
  uint8_t value;
  bool control;
  ticks_t time;
  
public:
  Token(uint8_t v = 0, bool c = false, ticks_t time = 0)
  : value(v), control(c), time(time) { }

  bool isControl() const {
    return control;
  }
  
  uint8_t getValue() const {
    return value;
  }

  bool isCtEnd() const {
    return control && value == CT_END;
  }

  bool isCtPause() const {
    return control && value == CT_PAUSE;
  }

  ticks_t getTime() const {
    return time;
  }

  operator uint8_t() const { return value; }

  bool operator==(const Token &other) const {
    return value == other.value &&
           control == other.control;
  }
};
  
} // End axe namespace

#endif // _Token_h_
