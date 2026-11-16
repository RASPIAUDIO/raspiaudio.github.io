#ifndef FACTORY_PROGRAM_TEST
#include "app_conf.h"
#include "rtos_macros.h"
#endif
#if defined(FACTORY_PROGRAM_TEST) || (FACTORY_PROVISION && ON_TILE(0))
#include "factory_program.h"
#include "factory_image.h"
#include <string.h>
#ifndef FACTORY_PROGRAM_TEST
#include "rtos_qspi_flash.h"
#include "platform/driver_instances.h"
#include "FreeRTOS.h"
#include "task.h"
static uint32_t fp_now(void){return xTaskGetTickCount()*portTICK_PERIOD_MS;}
static int fp_geometry(void){rtos_qspi_flash_t *f=get_qspi_flash_ctx();return
    rtos_qspi_flash_size_get(f)==0x400000 && rtos_qspi_flash_sector_size_get(f)==4096 &&
    rtos_qspi_flash_page_size_get(f)==256;}
static void fp_erase(uint32_t a){rtos_qspi_flash_erase(get_qspi_flash_ctx(),a,4096);}
static void fp_write(uint32_t a,const uint8_t *d,unsigned n){rtos_qspi_flash_write(get_qspi_flash_ctx(),d,a,n);}
static void fp_read(uint32_t a,uint8_t *d,unsigned n){rtos_qspi_flash_read(get_qspi_flash_ctx(),d,a,n);}
#endif
static volatile unsigned state, error;
static uint32_t next_erase, written, checked, crc, check_crc, touched;
static uint8_t first_page[256], scratch[256];
static uint32_t fp_crc(uint32_t c,const uint8_t *p,unsigned n){
    while(n--){c^=*p++;for(unsigned bit=0;bit<8;bit++)c=(c>>1)^(0xedb88320u & (0u-(c&1u)));}return c;
}
static int fail(unsigned code){error=code;state=FP_FAILED;return -1;}
void factory_program_authorize(void){if(state==FP_LOCKED){state=FP_READY;touched=fp_now();}}
unsigned factory_program_state(void){return state;}
void factory_program_poll(void){
    if(state>=FP_ERASING && state<=FP_VERIFYING && (uint32_t)(fp_now()-touched)>30000u)fail(9);
}
void factory_program_status(uint8_t out[16]){
    memset(out,0,16);memcpy(out,"FPG1",4);out[4]=state;out[5]=error;
    uint32_t progress=state==FP_ERASING?next_erase:state==FP_VERIFYING?checked:written;
    memcpy(out+8,&progress,4);uint32_t expected=FACTORY_IMAGE_CRC;memcpy(out+12,&expected,4);
}
int factory_program_packet(const uint8_t *p,size_t n){
    if(n!=132 || !p || state==FP_LOCKED || state==FP_FAILED || state==FP_VERIFIED)return -1;
    factory_program_poll();if(state==FP_FAILED)return -1;
    uint32_t word;memcpy(&word,p,4);unsigned op=word>>24;uint32_t address=word&0xffffffu;
    touched=fp_now();
    if(op==1 && state==FP_READY){
        uint32_t expected;memcpy(&expected,p+4,4);
        if(address!=FACTORY_IMAGE_BYTES || expected!=FACTORY_IMAGE_CRC ||
           address<4096 || address>0x200000 || (address%4096) || !fp_geometry())return fail(1);
        // Invalidate the old boot header first; commit the replacement header last.
        fp_erase(0);next_erase=4096;written=checked=0;crc=check_crc=0xffffffffu;
        memset(first_page,0xff,sizeof(first_page));state=FP_ERASING;return 0;
    }
    if(op==2 && state==FP_ERASING && address==next_erase && address<0x200000){
        fp_erase(address);next_erase+=4096;
        if(next_erase==0x200000)state=FP_WRITING;return 0;
    }
    if(op==3 && state==FP_WRITING && address==written && address<FACTORY_IMAGE_BYTES){
        if(address<256)memcpy(first_page+address,p+4,128);
        else {
            fp_write(address,p+4,128);fp_read(address,scratch,128);
            if(memcmp(scratch,p+4,128))return fail(3);
        }
        crc=fp_crc(crc,p+4,128);written+=128;return 0;
    }
    if(op==4 && state==FP_WRITING && address==FACTORY_IMAGE_BYTES && written==address){
        if((crc^0xffffffffu)!=FACTORY_IMAGE_CRC)return fail(4);
        fp_write(0,first_page,256);fp_read(0,scratch,256);
        if(memcmp(scratch,first_page,256))return fail(5);
        state=FP_VERIFYING;return 0;
    }
    if(op==5 && state==FP_VERIFYING && address==checked && address<FACTORY_IMAGE_BYTES){
        for(unsigned a=0;a<4096;a+=256){
            fp_read(address+a,scratch,256);check_crc=fp_crc(check_crc,scratch,256);
        }
        checked+=4096;
        if(checked==FACTORY_IMAGE_BYTES){
            if((check_crc^0xffffffffu)!=FACTORY_IMAGE_CRC)return fail(6);
            state=FP_VERIFIED;
        }
        return 0;
    }
    return fail(2);
}
#endif
