/*******************************************************************************
 *  A simple kernel for the raspberry pi zero
 ******************************************************************************/

#include <stdint.h>
//#include <string.h>

#include "framebuffer.h"
#include "display.h"
#include "bitfont.h"
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

// #define GPFSEL3 0x2020000C
// #define GPFSEL4 0x20200010
// #define GPSET1  0x20200020
// #define GPCLR1  0x2020002C
//
// Timer register CLO
// ##define TIMER_LO 0x20003004

//define some colors for 16bit 565 
#define RGB16_DARK_BLUE 0x0007
#define RGB16_WHITE 0xffff

//the weird double cast is so it works in 32bit and 64bit without warnings
static volatile uint32_t* dlist_memory = (uint32_t*)(uintptr_t)SCALER_LIST_MEMORY;
static volatile struct pixel_value* pv=(volatile struct pixel_value*)(uintptr_t)(PV2_BASE);
static volatile struct hvs_channel* hvs_channels=(volatile struct hvs_channel*)(uintptr_t)(SCALER_DISPCTRL0);

//writes a plane in displaylist format to the specified dl offset
void write_plane(uint16_t* offset, hvs_plane plane)
{
    // Control Word
    const uint8_t number_of_words = 7;
    uint32_t control_word = SCALER_CTL0_VALID              |        // denotes the start of a plane
                            SCALER_CTL0_UNITY              |        // indicates no scaling
                            plane.pixel_order       << 13  |        // pixel order
                            number_of_words         << 24  |        // number of words in this plane
                            plane.format;                           // pixel format
    dlist_memory[(*offset)++] = control_word;

    // Position/size
    uint32_t position_word = plane.start_x << 0 | plane.start_y << 12;
    uint32_t size_word =     plane.width << 0 | plane.height << 16;
    dlist_memory[(*offset)++] = position_word;
    dlist_memory[(*offset)++] = size_word;

    //used by HVS
    dlist_memory[(*offset)++] = 0;

    //fb pointer (this is physical memory as seen from the hvs memory space - so we need the top bit set)
    uint32_t framebuffer = (uint32_t)(uintptr_t)plane.framebuffer;
    dlist_memory[(*offset)++] = (0x80000000 | framebuffer);

    //used by HVS
    dlist_memory[(*offset)++] = 0;

    //pitch
    dlist_memory[(*offset)++] = plane.pitch;
}

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

  uint32_t width = (hvs_channels[1].dispctrl >> 12) & 0xfff;
  uint32_t height = hvs_channels[1].dispctrl & 0xfff;
  uint32_t interlaced = (pv->vc & (1<<4))!=0;
  uint32_t clk_select = (pv->c & (3<<2))>>2;  //1 for hdmi, 2 for sdtv (vec)
  uint32_t ntsc_pal;

  pv->int_enable = 0;
  pv->int_status = 0xffffffff;
  pv->int_enable = 0xffffffff;

  uint16_t* const fb = (uint16_t*)(0x10000000);     //hard code the small frame buffer to 256mbyte, any address is good
  const uint16_t fb_width = 320, fb_height = 120;

  //create the frame buffer plane in the middle of the screen so its not near the corners
  //this plane will be visible all the time so keep it a dark color so it doesn't bleed
  //light to the photo sensor.
  hvs_plane plane = {
      .format = HVS_PIXEL_FORMAT_RGB565,
      .pixel_order = HVS_PIXEL_ORDER_ARGB,
      .start_x = (width-fb_width)/2,
      .start_y = (height-fb_height)/2,
      .height = fb_height,
      .width = fb_width,
      .pitch = fb_width * sizeof(uint16_t),
      .framebuffer = fb
  };

  //clear to dark blue
  for (int i = 0; i < plane.width * plane.height; i++) {
      fb[i] = RGB16_DARK_BLUE;
  }

  //make the displaylist for the plane at offset 0 and enable it
  //anything not convered by the plane will be the background color
  uint16_t offset = 0;
  write_plane(&offset, plane);

  //terminate the displaylist (we only draw a single plane)
  dlist_memory[offset] = SCALER_CTL0_END;

  //HVS channel 1 display list is set by SCALER_DISPLIST1 (this drives HDMI/SDTV via PV2)
  *((volatile uint32_t*)SCALER_DISPLIST1) = 0;  //offset in dl memory


  //0xRRGGBB, bit 24 (SCALER_DISPBKGND_FILL) is the enable
  //set to background to black
  //From now on we'll only manipulate the background color, the plane stays in the center
  hvs_channels[1].dispbkgnd = (interlaced?SCALER_DISPBKGND_INTERLACE:0)|SCALER_DISPBKGND_FILL|0x000000;

  //GPIO23 as output for the scope trigger so we can hardware debug
  //bits 11-9 function select for GPIO23 (001=output)
  uint32_t fs2;
  fs2 = GET32(GPFSEL2);
  fs2 &= ~(0x7<<9); 
  fs2 |= (1<<9);      //001 = output

  //GPIO24 as input from the detector
  //14-12 function select for GPIO24 (000=input)
  fs2 &= ~(0x7<<12);
  PUT32(GPFSEL2, fs2);

  //gpio23 to low
  *((volatile uint32_t*)GPCLR0) = (1<<23);

  fb_info_t fbInfo;
  initializeFrameBuffer(&fbInfo, 1920, 1080, 32);
  // renderString(&fbInfo, "Hello, world!");

  unsigned int ra;

  ra=GET32(GPFSEL4);
  ra&=~(7<<21);
  ra|=1<<21;
  PUT32(GPFSEL4,ra);

  while (1){
    uint32_t time_start;
    uint32_t time_end;

    //this black might tear because its not synchronized to anything.
    //Its ok because we're going to let it settle for a couple seconds.
    hvs_channels[1].dispbkgnd = (interlaced?SCALER_DISPBKGND_INTERLACE:0)|SCALER_DISPBKGND_FILL|0x000000;

    for(ra=0;ra<0x100000;ra++) dummy(ra);

    //stage#1 wait for the end of the current frame
    //        set white which will be visible on the first pixel of line 0
    while(1) {
      uint32_t line = hvs_channels[1].dispstat &0xfff;
      //NOTE: In 1080P ctive lives are 0 to 1079, the register goes to 1080 at the end of the frame
      if ((line>=height))
      {
        hvs_channels[1].dispbkgnd = (interlaced?SCALER_DISPBKGND_INTERLACE:0)|SCALER_DISPBKGND_FILL|0xffffff;

        //clear all the PV interrupt flags
        uint32_t stat = pv->int_status;
        pv->int_status=stat;
        break;
      }
    }

    //stage#2 wait for PV to indicate active scanout
    //        we are now outputting the first line of white
    while(1) {
      uint32_t stat = pv->int_status;
      if (stat & PV_INTEN_VACT_START) {
        //take timestamp
        //the time delay from now until the input GPIO24 with the photodetector goes low is the TV latency
        time_start = *((volatile uint32_t*)TIMER_LO);
        pv->int_status = stat;  //clear any pending interrupts status bits

        //turn on the output GPIO
        *((volatile uint32_t*)GPSET0) = (1<<23);
        break;
      }
    }

    //stage#3 wait for the photo detector on GPIO24 to be low (detecting light)
    while(1){
      uint32_t input0=*((volatile uint32_t*)GPLEVEL0);
      if ((input0 & (1<<24))==0) {   //IF GPIO24 is low
        time_end = *((volatile uint32_t*)TIMER_LO);

        //turn off GPIO 23 so the delay can be timed externally on a scope
        // *((volatile uint32_t*)GPCLR0) = (1<<23);
        break;        
      }
    }


    //
    //leave white screen on until the end of the frame and go black for the next frame
    //(this isn't really needed but stops ugly tearing)
    while(1) {
      uint32_t line = hvs_channels[1].dispstat &0xfff;
      if ((line>=height)) {
        hvs_channels[1].dispbkgnd = (interlaced?SCALER_DISPBKGND_INTERLACE:0)|SCALER_DISPBKGND_FILL|0x000000;
        break;
      }
    }

    //draw the result on the small frame buffer centered on the screen
    //Clear the whole frame buffer to dark blue and draw the result text
    for (int i = 0; i < plane.width * plane.height; i++) {
        fb[i] = RGB16_DARK_BLUE;
    }

    //make the result text
    char buffer[64];
    sprintf(buffer,"Elapsed time = %d us", time_end-time_start);
    // DrawBitFont(&plane,&Arial_16,0,0,buffer,RGB16_WHITE);


    // time_start = *((volatile uint32_t*)TIMER_LO);
    //
    // for(int x = 0; x< 2; x++){
    //   for(int y = 0; y< 2; y++){
    //     fbPutPixel (&fbInfo, 800+x, 500+y, FOREGROUND_COLOR);
    //   }
    //  }
    // // memset(pixel_start, FOREGROUND_COLOR, fill_len);
    //
    // time_end = *((volatile uint32_t*)TIMER_LO);
    //
    // //wait
    // for(ra=0;ra<0x100000;ra++) dummy(ra);
    // // delay_usec(2000000);
    //
    // for(int x = 0; x< 20; x++){
    //   for(int y = 0; y< 20; y++){
    //     fbPutPixel (&fbInfo, 800+x, 500+y, BACKGROUND_COLOR);
    //   }
    // }
    // // memset(pixel_start, BACKGROUND_COLOR, fill_len);
    //
    //char buffer[64];
    //sprintf(buffer, "Elapsed time = %d us", time_end-time_start);
    renderString(&fbInfo, buffer);

    // //wait
    // for(ra=0;ra<0x100000;ra++) dummy(ra);
    // // delay_usec(2000000);

  }


}
