// Host must first observe USB absent. Never poll this during USB recovery/DFU.
static void safeDiagnostic(bool reset) {
  if(millis()<15000){Serial.println("SAFE_DIAG wait_for_boot_window");return;}
  Wire.end();releaseBus();
  uint32_t quiet=micros();
  while((uint32_t)(micros()-quiet)<1000000u) {
    if(!digitalRead(SDA_PIN)||!digitalRead(SCL_PIN)) {
      Serial.println("SAFE_DIAG active_bus_refused");return;
    }
    delayMicroseconds(2);
  }
  Wire.setBufferSize(256);Wire.setTimeOut(250);
  if(!Wire.begin(SDA_PIN,SCL_PIN,100000))return;
  // I2S waits for host bootstrap before exposing the SDK control endpoint.
  uint8_t boot[16]={0};
  unsigned received=Wire.requestFrom((uint8_t)0x2c,(uint8_t)16);
  for(unsigned i=0;i<received && i<16;i++)boot[i]=Wire.read();
  uint16_t crc=0xffff;
  for(unsigned i=0;i<14;i++){
    crc^=(uint16_t)boot[i]<<8;
    for(unsigned b=0;b<8;b++)crc=(crc&0x8000)?(crc<<1)^0x1021:crc<<1;
  }
  bool bootstrap=received==16 && !memcmp(boot,"V2BT\1\0\6\0\x30",9)
    && crc==(uint16_t)(boot[14]|((unsigned)boot[15]<<8));
  Serial.printf("SAFE_BOOT bytes=%u identity=",received);
  for(auto v:boot)Serial.printf("%02x",v);
  Serial.printf(" valid=%u\n",bootstrap);
  uint8_t version[3]={0},caps[8]={0};
  bool ok=!bootstrap && sdkRead(0,version,3) && sdkRead(16,caps,8);
  Serial.printf("SAFE_I2S ready=%u version=%u.%u.%u caps=",ok,version[0],version[1],version[2]);
  for(auto v:caps)Serial.printf("%02x",v);
  Serial.println();
  if(reset && (bootstrap || (ok && !memcmp(caps,"V2UI",4)))) {
    ok=pcalWrite(2,0) && pcalWrite(6,0xe6);
    delay(20);ok=ok && pcalWrite(6,0xe7);
    Serial.println(ok?"SAFE_QSPI_RESET no_SPI_reload":"SAFE_QSPI_RESET_FAILED");
  }
  Wire.end();releaseBus();
}
