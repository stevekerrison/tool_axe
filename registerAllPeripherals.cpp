// Copyright (c) 2011, Richard Osborne, All rights reserved
// This software is freely distributable under a derivative of the
// University of Illinois/NCSA Open Source License posted in
// LICENSE.txt and at <http://github.xcore.com/>

#include "registerAllPeripherals.h"
#include "PeripheralRegistry.h"
#include "UartRx.h"
#include "SPIFlash.h"
#include "EthernetPhy.h"
#include "PeripheralDescriptor.h"

void registerAllPeripherals()
{
  PeripheralRegistry::add(getPeripheralDescriptorUartRx());
  PeripheralRegistry::add(getPeripheralDescriptorSPIFlash());
  PeripheralRegistry::add(getPeripheralDescriptorEthernetPhy());
}
