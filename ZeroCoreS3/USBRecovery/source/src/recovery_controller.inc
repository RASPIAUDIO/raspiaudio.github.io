// Included by the ZeroCore factory fixture, not by production Home Assistant.
#ifndef FACTORY_CONTROLLER_TEST
#include <Wire.h>
#include <mbedtls/sha256.h>
#include "production_image.h"
extern const uint8_t production_start[] asm("_binary_images_production_bin_start");
extern const uint8_t production_end[] asm("_binary_images_production_bin_end");
#endif
static bool installAttempted, installComplete;
static uint32_t installWaitStarted;
static bool sdkRead(uint8_t cmd,uint8_t *out,unsigned n) {
  if(n>56)return false;
  const uint32_t started=millis();
  for(unsigned attempt=0;attempt<20 && millis()-started<1500;attempt++) {
    Wire.beginTransmission(0x2c);Wire.write(0x30);Wire.write(cmd|0x80);Wire.write(n+1);
    const unsigned tx=Wire.endTransmission();
    if(tx!=0){Serial.printf("STATUS_RETRY cmd=%u tx=%u\n",cmd,tx);delay(10);continue;}
    delay(5);
    const unsigned got=Wire.requestFrom((uint8_t)0x2c,(uint8_t)(n+1));
    if(got!=n+1){
      while(Wire.available())Wire.read();
      Serial.printf("STATUS_RETRY cmd=%u bytes=%u expected=%u\n",cmd,got,n+1);
      delay(10);continue;
    }
    uint8_t status=Wire.read();for(unsigned i=0;i<n;i++)out[i]=Wire.read();
    if(status==64 && out[6])Serial.printf("WRITER_BUSY stage=%u\n",out[6]);
    if(!status)return true;
    if(status!=64)return false;
    delay(5);
  }
  return false;
}
static uint32_t little32(const uint8_t *p){uint32_t v;memcpy(&v,p,4);return v;}
static bool programStatus(uint8_t *s) {
  return sdkRead(20,s,16) && !memcmp(s,"RUP2",4) &&
      little32(s+12)==PRODUCTION_CRC && s[5]==0 && s[4]!=6;
}
static bool sendProgram(unsigned op,uint32_t offset,const uint8_t *data,
                        unsigned expectedState,uint32_t progress) {
  uint8_t packet[132]={0};uint32_t word=(op<<24)|offset;memcpy(packet,&word,4);
  if(data)memcpy(packet+4,data,128);
  Wire.beginTransmission(0x2c);Wire.write(0x30);Wire.write(19);Wire.write(sizeof(packet));
  if(Wire.write(packet,sizeof(packet))!=sizeof(packet)) {
    Serial.printf("INSTALL_TRANSPORT short_command op=%u offset=%lu\n",op,(unsigned long)offset);
    return false;
  }
  const unsigned tx=Wire.endTransmission();
  if(tx)Serial.printf("INSTALL_TRANSPORT uncertain_ack op=%u offset=%lu tx=%u\n",op,(unsigned long)offset,tx);
  // Never repeat a destructive command after a transport error. Only poll status.
  uint32_t start=millis();uint8_t s[16];
  while(millis()-start<6000) {
    delay(10);
    if(!sdkRead(20,s,16))continue;
    if(memcmp(s,"RUP2",4) || little32(s+12)!=PRODUCTION_CRC || s[5] || s[4]==6) {
      Serial.printf("INSTALL_STATUS_FAILED op=%u offset=%lu state=%u error=%u progress=%lu\n",
                    op,(unsigned long)offset,s[4],s[5],(unsigned long)little32(s+8));
      return false;
    }
    if(s[4]==expectedState && little32(s+8)==progress)return true;
  }
  Serial.printf("INSTALL_STATUS_TIMEOUT op=%u offset=%lu expected_state=%u progress=%lu\n",
                op,(unsigned long)offset,expectedState,(unsigned long)progress);
  return false;
}
static bool pcalWrite(uint8_t reg,uint8_t value){
  Wire.beginTransmission(0x20);Wire.write(reg);Wire.write(value);return Wire.endTransmission()==0;
}
static bool installProduction(void) {
  const size_t n=production_end-production_start;
  uint8_t digest[32];
  if(n!=PRODUCTION_BYTES || mbedtls_sha256_ret(production_start,n,digest,0) ||
     memcmp(digest,production_sha256,32)){Serial.println("INSTALL_ABORT payload_SHA256");return false;}
  // XMOS signalled explicit bus handoff on MISO. SPI clock stays high impedance.
  for(int pin:{10,11,12})release(pin);
  Wire.setBufferSize(256);Wire.setTimeOut(250);
  if(!Wire.begin(SDA_PIN,SCL_PIN,100000))return false;
  delay(300);
  uint8_t s[16]={0};
  if(!programStatus(s) || s[4]!=1) {
    Serial.printf("WRITER_STATUS state=%u error=%u stage=%u\n",s[4],s[5],s[6]);
    Serial.println("INSTALL_ABORT recovery_writer_not_ready");return false;
  }
  uint8_t begin[128]={0};uint32_t crc=PRODUCTION_CRC;memcpy(begin,&crc,4);
  Serial.printf("INSTALL_BEGIN bytes=%u crc=%08lx SHA256_verified transport=retry-read-v1\n",(unsigned)n,(unsigned long)crc);
  if(!sendProgram(1,n,begin,2,4096))return false;
  for(uint32_t a=4096;a<0x200000;a+=4096){
    unsigned next=a+4096==0x200000?3:2;
    if(!sendProgram(2,a,nullptr,next,next==3?0:a+4096))return false;
    if(!(a%0x20000))Serial.printf("INSTALL_ERASE %lu/2097152\n",(unsigned long)(a+4096));
  }
  for(uint32_t a=0;a<n;a+=128) {
    if(!sendProgram(3,a,production_start+a,3,a+128))return false;
    if(!((a+128)%32768))Serial.printf("INSTALL_WRITE %lu/%u\n",(unsigned long)(a+128),(unsigned)n);
  }
  if(!sendProgram(4,n,nullptr,4,0))return false;
  for(uint32_t a=0;a<n;a+=4096) {
    unsigned next=a+4096==n?5:4;
    if(!sendProgram(5,a,nullptr,next,next==5?n:a+4096))return false;
  }
  Serial.println("INSTALL_QSPI_VERIFIED header_and_image_readback_OK");
  delay(500);
  if(!pcalWrite(2,0) || !pcalWrite(6,0xe6))return false;
  delay(20);
  if(!pcalWrite(6,0xe7))return false;
#if PRODUCTION_EXPECT_USB_BOOT
  // The USB application becomes I2C master. Never probe it as an I2C slave.
  Wire.end();releaseBus();
  Serial.println("INSTALL_QSPI_VERIFIED USB_boot_requires_host_check no_SPI_reload");
  return true;
#else
  delay(3000);
  uint8_t caps[8],version[3];
  if(!sdkRead(0,version,3) || memcmp(version,"\0\6\0",3) ||
     !sdkRead(16,caps,8) || memcmp(caps,"V2UI",4) || caps[4]!=2) {
    Serial.println("INSTALL_QSPI_VERIFIED runtime_boot_unconfirmed");return false;
  }
  Serial.println("INSTALL_SUCCESS I2S_0.6.0_UI2_QSPI_boot_verified no_SPI_reload");
  return true;
#endif
}

static void recoveryLoop() {
  if(!handedOff || installAttempted)return;
  if(!installWaitStarted)installWaitStarted=millis();
  if(millis()-installWaitStarted<3000 || !digitalRead(13))return;
  delay(50);if(!digitalRead(13))return;
  installAttempted=true;installComplete=installProduction();
  if(installComplete){Preferences p;if(p.begin("safe-install",false)){p.putUInt("verified-crc",PRODUCTION_CRC);p.end();}}
  if(!installComplete)Serial.println("INSTALL_FAILED no_automatic_retry");
  Wire.end();releaseBus();
}
