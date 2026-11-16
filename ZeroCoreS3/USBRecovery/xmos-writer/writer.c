/* Dedicated RAM provisioner: no audio, USB, RTOS or factory-test gate.
 * I2C HIL and the flash worker use separate cores. Flash operations never
 * block I2C callbacks; the host polls a locked status snapshot. */
#include <stdint.h>
#include <string.h>
#include <xs1.h>
#include <quadflashlib.h>
#include <xcore/parallel.h>
#include <xcore/hwtimer.h>
#include <xcore/port.h>
#include <xcore/lock.h>
#include "i2c.h"
#include "factory_image.h"
static lock_t mutex;
static uint8_t packet[132], snapshot[17], rx[135], reply[17];
static unsigned pending, busy, received, read_at;
static int flash_error;
static uint32_t fp_now(void) {
    static uint32_t previous, ms;
    uint32_t now=get_reference_time(), elapsed=(now-previous)/100000u;
    previous+=elapsed*100000u;ms+=elapsed;return ms;
}
static int fp_geometry(void) {
    return !flash_error && fl_getFlashSize()==0x400000 && fl_getPageSize()==256 &&
        fl_getNumSectors()==1024 && fl_getSectorSize(0)==4096;
}
static int command(unsigned op,const uint8_t *in,unsigned ni,uint8_t *out,unsigned no) {
    uint8_t unused=0;
    return fl_command(op,ni?(uint8_t *)in:&unused,ni,no?out:&unused,no);
}
static int wait_flash(void) {
    uint8_t sr;uint32_t began=fp_now();
    do {
        if(command(0x05,0,0,&sr,1))return -1;
        if(fp_now()-began>1500)return -1;
    } while(sr&1);
    return 0;
}
static void fp_erase(uint32_t a) {
    uint8_t address[3]={a>>16,a>>8,a};
    if(command(0x06,0,0,0,0) || command(0x20,address,3,0,0) || wait_flash())flash_error=10;
}
static void fp_read(uint32_t a,uint8_t *dst,unsigned n) {
    uint8_t address[3]={a>>16,a>>8,a};
    if(n>256 || (a%256)+n>256 || command(0x03,address,3,dst,n)) {flash_error=11;memset(dst,0,n);}
}
static void fp_write(uint32_t a,const uint8_t *data,unsigned n) {
    uint8_t message[259]={a>>16,a>>8,a};
    if(n>256 || (a%256)+n>256) {flash_error=12;return;}
    memcpy(message+3,data,n);
    if(command(0x06,0,0,0,0) || command(0x02,message,n+3,0,0) || wait_flash())flash_error=13;
}
#define FACTORY_PROGRAM_TEST 1
#define ON_TILE(n) ((n)==0)
#include "factory_program.c"

I2C_CALLBACK_ATTR static i2c_slave_ack_t ack_read(void *ctx) {
    (void)ctx;read_at=0;lock_acquire(mutex);
    memcpy(reply,snapshot,17);if(busy)reply[0]=64;
    lock_release(mutex);return I2C_SLAVE_ACK;
}
I2C_CALLBACK_ATTR static i2c_slave_ack_t ack_write(void *ctx) {
    (void)ctx;received=0;return I2C_SLAVE_ACK;
}
I2C_CALLBACK_ATTR static uint8_t read_byte(void *ctx) {
    (void)ctx;return read_at<17?reply[read_at++]:0xff;
}
I2C_CALLBACK_ATTR static i2c_slave_ack_t write_byte(void *ctx,uint8_t value) {
    (void)ctx;
    if(received>=sizeof(rx))return I2C_SLAVE_NACK;
    if(received==1 && value==19) {
        lock_acquire(mutex);unsigned unavailable=busy;lock_release(mutex);
        if(unavailable)return I2C_SLAVE_NACK;
    }
    rx[received++]=value;return I2C_SLAVE_ACK;
}
I2C_CALLBACK_ATTR static void stop(void *ctx) {
    (void)ctx;
    if(received==135 && rx[0]==0x30 && rx[1]==19 && rx[2]==132) {
        lock_acquire(mutex);
        if(!busy){memcpy(packet,rx+3,132);pending=1;busy=1;}
        lock_release(mutex);
    }
    received=0;
}
I2C_CALLBACK_ATTR static int shutdown(void *ctx){(void)ctx;return 0;}
DECLARE_JOB(worker,(void));
DECLARE_JOB(i2c_slave,(const i2c_callback_group_t *,port_t,port_t,uint8_t));
static void publish(void) {
    uint8_t status[17]={0};factory_program_status(status+1);
    memcpy(status+1,"RUP2",4);
    lock_acquire(mutex);memcpy(snapshot,status,17);busy=0;lock_release(mutex);
}
static void startup_stage(unsigned stage) {
    lock_acquire(mutex);snapshot[7]=stage;lock_release(mutex);
}
static int enable_winbond_quad(void) {
    uint8_t id[3], sr2, sr1, unused=0;
    startup_stage(6);
    if(fl_command(0x9f,&unused,0,id,3))return 19;
    if(id[0]!=0xef || (id[1]!=0x40 && id[1]!=0x60) || id[2]!=0x16)return 20;
    startup_stage(7);
    if(fl_command(0x35,&unused,0,&sr2,1))return 21;
    if(sr2&2)return 0;
    startup_stage(8);
    // Preserve all other status bits; never change protection or OTP bits.
    sr2|=2;
    if(fl_command(0x06,&unused,0,&unused,0) || fl_command(0x31,&sr2,1,&unused,0))return 22;
    uint32_t began=fp_now();
    do {
        if(fl_command(0x05,&unused,0,&sr1,1))return 23;
        if(fp_now()-began>1000)return 24;
    } while(sr1&1);
    if(fl_command(0x35,&unused,0,&sr2,1) || !(sr2&2))return 25;
    return 0;
}
void worker(void) {
    startup_stage(1);
    // The SPI loader shares the QSPI clock. Let the S3 release all SPI pins.
    hwtimer_t t=hwtimer_alloc();hwtimer_delay(t,100000000);hwtimer_free(t);
    fl_QSPIPorts ports={XS1_PORT_1B,XS1_PORT_1C,XS1_PORT_4B,XS1_CLKBLK_1};
    startup_stage(2);
    if(fl_connect(&ports))flash_error=14;
    startup_stage(3);
    // Keep the library's clock configuration established by fl_connect.
    startup_stage(4);
    if(!flash_error && !fp_geometry())flash_error=17;
    startup_stage(5);
    if(!flash_error)flash_error=enable_winbond_quad();
    if(flash_error)fail(flash_error);else factory_program_authorize();
    publish();
    for(;;) {
        uint8_t local[132];unsigned execute;
        lock_acquire(mutex);execute=pending;
        if(execute){memcpy(local,packet,132);pending=0;}
        lock_release(mutex);
        if(execute) {
            factory_program_packet(local,132);
            if(flash_error)fail(flash_error);
            if(factory_program_state()==FP_VERIFIED && fl_disconnect())fail(15);
            publish();
        }
        fp_now();
    }
}
void recovery_writer(void) {
    mutex=lock_alloc();busy=1;
    factory_program_status(snapshot+1);memcpy(snapshot+1,"RUP2",4);
    port_enable(XS1_PORT_1P);port_out(XS1_PORT_1P,1);
    i2c_callback_group_t callbacks={
        .ack_read_request=ack_read,.ack_write_request=ack_write,
        .master_requires_data=read_byte,.master_sent_data=write_byte,
        .stop_bit=stop,.shutdown=shutdown,.app_data=0};
    PAR_JOBS(PJOB(i2c_slave,(&callbacks,XS1_PORT_1N,XS1_PORT_1O,0x2c)),PJOB(worker,()));
}
