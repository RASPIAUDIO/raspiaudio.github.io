// UM10204 bus clear, only before handoff and after observing a static bus.
static bool clearStalledBus() {
  if(handedOff)return false;
  releaseBus();
  int sda=digitalRead(SDA_PIN),scl=digitalRead(SCL_PIN);
  if(sda && scl)return true;
  if(sda || !scl)return false;
  uint32_t begin=micros();
  while((uint32_t)(micros()-begin)<100000u) {
    if(digitalRead(SDA_PIN)!=sda || digitalRead(SCL_PIN)!=scl) {
      Serial.println("BUS_CLEAR_REFUSED activity");return false;
    }
    delayMicroseconds(2);
  }
  Serial.println("BUS_CLEAR static_SDA_low max_9_clocks");
  fault=false;
  unsigned pulses=0;
  while(!digitalRead(SDA_PIN) && pulses<9) {
    low(SCL_PIN);tick();
    if(!highClock()){releaseBus();return false;}
    ++pulses;
  }
  // Terminate the interrupted transfer with STOP, without driving either line high.
  low(SCL_PIN);tick();low(SDA_PIN);tick();
  if(!highClock()){releaseBus();return false;}
  release(SDA_PIN);tick();releaseBus();
  bool ok=digitalRead(SDA_PIN) && digitalRead(SCL_PIN);
  Serial.printf("BUS_CLEAR result=%u pulses=%u SDA=%d SCL=%d\n",ok,pulses,digitalRead(SDA_PIN),digitalRead(SCL_PIN));
  return ok;
}
