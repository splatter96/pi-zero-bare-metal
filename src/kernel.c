/*******************************************************************************
 *  A simple kernel for the raspberry pi zero
 ******************************************************************************/

#include <stdint.h>
#include <string.h>

#include "framebuffer.h"
#include "mailbox.h"
#include "ascii.h"
#include "printf.h"

/* defined in the linker script */
extern int _bss_start_;
extern int _bss_end_;

extern void PUT32 ( unsigned int, unsigned int );
extern unsigned int GET32 ( unsigned int );
extern void dummy ( unsigned int );
extern void delay_usec (uint32_t us);

#define GPFSEL3 0x2020000C
#define GPFSEL4 0x20200010
#define GPSET1  0x20200020
#define GPCLR1  0x2020002C

// Timer register CLO
#define TIMER_LO 0x20003004

/*
 *  Initializse the c environment (partially for now). 
 *  Initialize static variables to zero
 */
void init ()
{
  int * bss = &_bss_start_;
  int * bssEnd = &_bss_end_;

  while (bss < bssEnd) {
    *bss = 0;
    bss++;
  }
}


/*
 * kernel main function, entry point for C code after the ARM Assembly init
 */
void kernel_main ()
{
  init();

  fb_info_t fbInfo;
  initializeFrameBuffer(&fbInfo, 1920, 1080, 32);
  // renderString(&fbInfo, "Hello, world!");

  unsigned int ra;

  ra=GET32(GPFSEL4);
  ra&=~(7<<21);
  ra|=1<<21;
  PUT32(GPFSEL4,ra);

  uint32_t start_x =800, start_y=500;
  uint32_t len_x =50, len_y=50;
  uint32_t fill_len = len_y * fbInfo.pitch + len_x;
  uint32_t offset = (start_y * fbInfo.pitch) + (start_x << 2);
  uint32_t * pixel_start = (uint32_t *) (fbInfo.fb + offset);

  while (1){
    uint32_t time_start;
    uint32_t time_end;

    time_start = *((volatile uint32_t*)TIMER_LO);

    // for(int x = 0; x< 2; x++){
    //   for(int y = 0; y< 2; y++){
    //     fbPutPixel (&fbInfo, 800+x, 500+y, FOREGROUND_COLOR);
    //   }
    //  }
    //
    memset(pixel_start, FOREGROUND_COLOR, fill_len);

    time_end = *((volatile uint32_t*)TIMER_LO);

    //wait
    for(ra=0;ra<0x100000;ra++) dummy(ra);
    // delay_usec(2000000);

    // for(int x = 0; x< 20; x++){
    //   for(int y = 0; y< 20; y++){
    //     fbPutPixel (&fbInfo, 800+x, 500+y, BACKGROUND_COLOR);
    //   }
    // }
    memset(pixel_start, BACKGROUND_COLOR, fill_len);

    char buffer[64];
    sprintf(buffer, "Elapsed time = %d us", time_end-time_start);
    renderString(&fbInfo, buffer);

    //wait
    for(ra=0;ra<0x100000;ra++) dummy(ra);
    // delay_usec(2000000);

  }


}
