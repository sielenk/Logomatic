/*********************************************************************************
 * Logomatic V2 Firmware
 * Sparkfun Electronics 2008
 * ******************************************************************************/

/*******************************************************
 * 		     Header Files
 ******************************************************/
#include <stdio.h>
#include <string.h>
#include "LPC21xx.h"

// UART0 Debugging
extern "C" {
#include "serial.h"
#include "rprintf.h"
}

// Needed for main function calls
extern "C" {
#include "main_msc.h"
#include "fat.h"
#include "armVIC.h"
#include "itoa.h"
#include "rootdir.h"
#include "sd_raw.h"
#include "string_printf.h"
#include "delay.h"
}

/*******************************************************
 * 		     Global Variables
 ******************************************************/

#define ON 1
#define OFF 0

#define buf_size 512

namespace {
  char RX_array1[buf_size];
  char RX_array2[buf_size];
  char log_array1 = 0;
  char log_array2 = 0;
  short RX_in = 0;
  char get_frame = 0;

  signed int stringSize;
  struct fat_file_struct* handle;
  struct fat_file_struct* fd;
  char stringBuf[256];

  // Default Settings
  char mode = 0;
  char asc = 'N';
  int baud = 9600;
  int freq = 100;
  char trig = '$';
  short frame = 100;
  char ad1_7 = 'N';
  char ad1_6 = 'N';
  char ad1_3 = 'N';
  char ad1_2 = 'N';
  char ad0_4 = 'N';
  char ad0_3 = 'N';
  char ad0_2 = 'N';
  char ad0_1 = 'N';
}

/*******************************************************
 * 		 Function Declarations
 ******************************************************/

namespace local {
  void Initialize();

  void setup_uart0(int newbaud, char want_ints);

  void mode_0();
  void mode_1();
  void mode_2();
  void mode_action();

  void Log_init();
  void test();
  void stat(int statnum, int onoff);
  void AD_conversion(int regbank);

  void feed();

  void FIQ_Routine() __attribute__((interrupt("FIQ")));
  void SWI_Routine() __attribute__((interrupt("SWI")));
  void UNDEF_Routine() __attribute__((interrupt("UNDEF")));

  void fat_initialize();
}

/*******************************************************
 * 		     	MAIN
 ******************************************************/

int main() {
  enableFIQ();

  local::Initialize();

  local::setup_uart0(9600, 0);

  local::fat_initialize();

  // Flash Status Lights
  for (int i = 0; i < 5; i++) {
    local::stat(0, ON);
    delay_ms(50);
    local::stat(0, OFF);
    local::stat(1, ON);
    delay_ms(50);
    local::stat(1, OFF);
  }

  local::Log_init();

  char name[32];
  int count = 1;

  for (;;) {
    string_printf(name, "LOG%02d.txt", count);

    if (!root_file_exists(name)) {
      break;
    }

    ++count;
    if (count == 250) {
      rprintf("Too Many Logs!\n\r");

      while (1) {
        local::stat(0, ON);
        local::stat(1, ON);
        delay_ms(1000);
        local::stat(0, OFF);
        local::stat(1, OFF);
        delay_ms(1000);
      }
    }
  }

  handle = root_open_new(name);

  sd_raw_sync();

  switch (mode) {
  case 0:
    local::mode_0();
    break;
  case 1:
    local::mode_1();
    break;
  case 2:
    local::mode_2();
    break;
  }

  return 0;
}

/*******************************************************
 * 		     Initialize
 ******************************************************/

#define PLOCK 0x400

void local::Initialize() {
  rprintf_devopen(putc_serial0);

  PINSEL0 = 0xCF351505;
  PINSEL1 = 0x15441801;
  IODIR0 |= 0x00000884;
  IOSET0 = 0x00000080;

  S0SPCR = 0x08;  // SPI clk to be pclk/8
  S0SPCR = 0x30;  // master, msb, first clk edge, active high, no ints
}

void local::feed() {
  PLLFEED = 0xAA;
  PLLFEED = 0x55;
}

namespace {
  void UART0ISR() {
    char temp;

    if (RX_in < buf_size) {
      RX_array1[RX_in] = U0RBR;

      RX_in++;

      if (RX_in == buf_size) log_array1 = 1;
    } else if (RX_in >= buf_size) {
      RX_array2[RX_in - buf_size] = U0RBR;
      RX_in++;

      if (RX_in == 2 * buf_size) {
        log_array2 = 1;
        RX_in = 0;
      }
    }

    temp = U0IIR;  // Have to read this to clear the interrupt

    VICVectAddr = 0;

    (void)temp;
  }

  void UART0ISR_2() {
    char temp;
    temp = U0RBR;

    if (temp == trig) {
      get_frame = 1;
    }

    if (get_frame) {
      if (RX_in < frame) {
        RX_array1[RX_in] = temp;
        RX_in++;

        if (RX_in == frame) {
          RX_array1[RX_in] = 10;  // delimiters
          RX_array1[RX_in + 1] = 13;
          log_array1 = 1;
          get_frame = 0;
        }
      } else if (RX_in >= frame) {
        RX_array2[RX_in - frame] = temp;
        RX_in++;

        if (RX_in == 2 * frame) {
          RX_array2[RX_in - frame] = 10;  // delimiters
          RX_array2[RX_in + 1 - frame] = 13;
          log_array2 = 1;
          get_frame = 0;
          RX_in = 0;
        }
      }
    }

    temp = U0IIR;  // have to read this to clear the interrupt

    VICVectAddr = 0;
  }

  void pushValue(char* q, int& ind, int temp2) {
    char temp_buff[4] = {};

    if (asc == 'Y') {
      itoa(temp2, 10, temp_buff);

      for (int i = 0; i < 4; ++i) {
        char const c(temp_buff[i]);

        if ('0' <= c && c <= '9') {
          q[ind++] = c;
        }
      }

      q[ind++] = 0;
    } else if (asc == 'N') {
      q[ind++] = static_cast<char>((temp2 >> 8) & 0xFF);
      q[ind++] = static_cast<char>(temp2 & 0xFF);
    }
  }

  void MODE2ISR() {
    int ind = 0;
    char q[50] = {};

    T0IR = 1;  // reset TMR0 interrupt

    // Get AD1.3
    if (ad1_3 == 'Y') {
      AD1CR = 0x0020FF08;   // AD1.3
      AD1CR |= 0x01000000;  // start conversion
      int temp(0);
      while ((temp & 0x80000000) == 0) {
        temp = AD1DR;
      }
      temp &= 0x0000FFC0;
      auto const temp2 = temp / 0x00000040;

      AD1CR = 0x00000000;

      pushValue(q, ind, temp2);
    }
    // Get AD0.3
    if (ad0_3 == 'Y') {
      AD0CR = 0x00020FF08;  // AD0.3
      AD0CR |= 0x01000000;  // start conversion
      int temp(0);
      while ((temp & 0x80000000) == 0) {
        temp = AD0DR;
      }
      temp &= 0x0000FFC0;
      auto const temp2 = temp / 0x00000040;

      AD0CR = 0x00000000;

      pushValue(q, ind, temp2);
    }
    // Get AD0.2
    if (ad0_2 == 'Y') {
      AD0CR = 0x00020FF04;  // AD1.2
      AD0CR |= 0x01000000;  // start conversion
      int temp(0);
      while ((temp & 0x80000000) == 0) {
        temp = AD0DR;
      }
      temp &= 0x0000FFC0;
      auto const temp2 = temp / 0x00000040;

      AD0CR = 0x00000000;

      pushValue(q, ind, temp2);
    }
    // Get AD0.1
    if (ad0_1 == 'Y') {
      AD0CR = 0x00020FF02;  // AD0.1
      AD0CR |= 0x01000000;  // start conversion
      int temp(0);
      while ((temp & 0x80000000) == 0) {
        temp = AD0DR;
      }
      temp &= 0x0000FFC0;
      auto const temp2 = temp / 0x00000040;

      AD0CR = 0x00000000;

      pushValue(q, ind, temp2);
    }
    // Get AD1.2
    if (ad1_2 == 'Y') {
      AD1CR = 0x00020FF04;  // AD1.2
      AD1CR |= 0x01000000;  // start conversion
      int temp(0);
      while ((temp & 0x80000000) == 0) {
        temp = AD1DR;
      }
      temp &= 0x0000FFC0;
      auto const temp2 = temp / 0x00000040;

      AD1CR = 0x00000000;

      pushValue(q, ind, temp2);
    }
    // Get AD0.4
    if (ad0_4 == 'Y') {
      AD0CR = 0x00020FF10;  // AD0.4
      AD0CR |= 0x01000000;  // start conversion
      int temp(0);
      while ((temp & 0x80000000) == 0) {
        temp = AD0DR;
      }
      temp &= 0x0000FFC0;
      auto const temp2 = temp / 0x00000040;

      AD0CR = 0x00000000;

      pushValue(q, ind, temp2);
    }
    // Get AD1.7
    if (ad1_7 == 'Y') {
      AD1CR = 0x00020FF80;  // AD1.7
      AD1CR |= 0x01000000;  // start conversion
      int temp(0);
      while ((temp & 0x80000000) == 0) {
        temp = AD1DR;
      }
      temp &= 0x0000FFC0;
      auto const temp2 = temp / 0x00000040;

      AD1CR = 0x00000000;

      pushValue(q, ind, temp2);
    }
    // Get AD1.6
    if (ad1_6 == 'Y') {
      AD1CR = 0x00020FF40;  // AD1.3
      AD1CR |= 0x01000000;  // start conversion
      int temp(0);
      while ((temp & 0x80000000) == 0) {
        temp = AD1DR;
      }
      temp &= 0x0000FFC0;
      auto const temp2 = temp / 0x00000040;

      AD1CR = 0x00000000;

      pushValue(q, ind, temp2);
    }

    for (int j = 0; j < ind; j++) {
      if (RX_in < 512) {
        RX_array1[RX_in] = q[j];
        RX_in++;

        if (RX_in == 512) log_array1 = 1;
      } else if (RX_in >= 512) {
        RX_array2[RX_in - 512] = q[j];
        RX_in++;

        if (RX_in == 1024) {
          log_array2 = 1;
          RX_in = 0;
        }
      }
    }
    if (RX_in < 512) {
      if (asc == 'N') {
        RX_array1[RX_in] = '$';
      } else if (asc == 'Y') {
        RX_array1[RX_in] = 13;
      }
      RX_in++;

      if (RX_in == 512) log_array1 = 1;
    } else if (RX_in >= 512) {
      if (asc == 'N')
        RX_array2[RX_in - 512] = '$';
      else if (asc == 'Y') {
        RX_array2[RX_in - 512] = 13;
      }
      RX_in++;

      if (RX_in == 1024) {
        log_array2 = 1;
        RX_in = 0;
      }
    }
    if (RX_in < 512) {
      if (asc == 'N')
        RX_array1[RX_in] = '$';
      else if (asc == 'Y') {
        RX_array1[RX_in] = 10;
      }
      RX_in++;

      if (RX_in == 512) log_array1 = 1;
    } else if (RX_in >= 512) {
      if (asc == 'N')
        RX_array2[RX_in - 512] = '$';
      else if (asc == 'Y') {
        RX_array2[RX_in - 512] = 10;
      }
      RX_in++;

      if (RX_in == 1024) {
        log_array2 = 1;
        RX_in = 0;
      }
    }

    VICVectAddr = 0;
  }
}

void local::FIQ_Routine() {
  char a;
  int j;

  local::stat(0, ON);
  for (j = 0; j < 5000000; j++)
    ;
  local::stat(0, OFF);
  a = U0RBR;

  a = U0IIR;  // have to read this to clear the interrupt

  (void)a;
}

void local::SWI_Routine() {
  while (1)
    ;
}

void local::UNDEF_Routine() { stat(0, ON); }

void local::setup_uart0(int newbaud, char want_ints) {
  baud = newbaud;
  U0LCR = 0x83;  // 8 bits, no parity, 1 stop bit, DLAB = 1

  switch (baud) {
  case 1200:
    U0DLM = 0x0C;
    U0DLL = 0x00;
    break;
  case 2400:
    U0DLM = 0x06;
    U0DLL = 0x00;
    break;
  case 4800:
    U0DLM = 0x03;
    U0DLL = 0x00;
    break;
  case 9600:
    U0DLM = 0x01;
    U0DLL = 0x80;
    break;
  case 19200:
    U0DLM = 0x00;
    U0DLL = 0xC0;
    break;
  case 38400:
    U0DLM = 0x00;
    U0DLL = 0x60;
    break;
  case 57600:
    U0DLM = 0x00;
    U0DLL = 0x40;
    break;
  case 115200:
    U0DLM = 0x00;
    U0DLL = 0x20;
    break;
  }

  U0FCR = 0x01;
  U0LCR = 0x03;

  if (want_ints == 1) {
    enableIRQ();
    VICIntSelect &= ~0x00000040;
    VICIntEnable |= 0x00000040;
    VICVectCntl1 = 0x26;
    VICVectAddr1 = (unsigned int)UART0ISR;
    U0IER = 0x01;
  } else if (want_ints == 2) {
    enableIRQ();
    VICIntSelect &= ~0x00000040;
    VICIntEnable |= 0x00000040;
    VICVectCntl2 = 0x26;
    VICVectAddr2 = (unsigned int)UART0ISR_2;
    U0IER = 0X01;
  } else if (want_ints == 0) {
    VICIntEnClr = 0x00000040;
    U0IER = 0x00;
  }
}

void local::stat(int statnum, int onoff) {
  if (statnum)  // Stat 1
  {
    if (onoff) {
      IOCLR0 = 0x00000800;
    }  // On
    else {
      IOSET0 = 0x00000800;
    }     // Off
  } else  // Stat 0
  {
    if (onoff) {
      IOCLR0 = 0x00000004;
    }  // On
    else {
      IOSET0 = 0x00000004;
    }  // Off
  }
}

void local::Log_init() {
  int x, mark = 0, ind = 0;
  char temp, temp2 = 0, safety = 0;
  //	signed char handle;

  if (root_file_exists("LOGCON.txt")) {
    // rprintf("\n\rFound LOGcon.txt\n");
    fd = root_open("LOGCON.txt");
    stringSize = fat_read_file(fd, (unsigned char*)stringBuf, 512);
    stringBuf[stringSize] = '\0';
    fat_close_file(fd);
  } else {
    // rprintf("Couldn't find LOGcon.txt, creating...\n");
    fd = root_open_new("LOGCON.txt");
    if (fd == NULL) {
      rprintf("Error creating LOGCON.txt, locking up...\n\r");
      while (1) {
        stat(0, ON);
        delay_ms(50);
        stat(0, OFF);
        stat(1, ON);
        delay_ms(50);
        stat(1, OFF);
      }
    }

    strcpy(stringBuf,
           "MODE = 0\r\nASCII = N\r\nBaud = 4\r\nFrequency = 100\r\nTrigger "
           "Character = $\r\nText Frame = 100\r\nAD1.3 = N\r\nAD0.3 = "
           "N\r\nAD0.2 = N\r\nAD0.1 = N\r\nAD1.2 = N\r\nAD0.4 = N\r\nAD1.7 = "
           "N\r\nAD1.6 = N\r\nSaftey On = Y\r\n");
    stringSize = strlen(stringBuf);
    fat_write_file(fd, (unsigned char*)stringBuf, stringSize);
    sd_raw_sync();
  }

  for (x = 0; x < stringSize; x++) {
    temp = stringBuf[x];
    if (temp == 10) {
      mark = x;
      ind++;
      if (ind == 1) {
        mode = stringBuf[mark - 2] -
               48;  // 0 = auto uart, 1 = trigger uart, 2 = adc
        rprintf("mode = %d\n\r", mode);
      } else if (ind == 2) {
        asc = stringBuf[mark - 2];  // default is 'N'
        rprintf("asc = %c\n\r", asc);
      } else if (ind == 3) {
        switch (stringBuf[mark - 2]) {
        case '1':
          baud = 1200;
          break;
        case '2':
          baud = 2400;
          break;
        case '3':
          baud = 4800;
          break;
        case '4':
          baud = 9600;
          break;
        case '5':
          baud = 19200;
          break;
        case '6':
          baud = 38400;
          break;
        case '7':
          baud = 57600;
          break;
        case '8':
          baud = 115200;
          break;
        }

        rprintf("baud = %d\n\r", baud);
      } else if (ind == 4) {
        freq = (stringBuf[mark - 2] - 48) + (stringBuf[mark - 3] - 48) * 10;
        if ((stringBuf[mark - 4] >= 48) && (stringBuf[mark - 4] < 58)) {
          freq += (stringBuf[mark - 4] - 48) * 100;
          if ((stringBuf[mark - 5] >= 48) && (stringBuf[mark - 5] < 58)) {
            freq += (stringBuf[mark - 5] - 48) * 1000;
          }
        }
        rprintf("freq = %d\n\r", freq);
      } else if (ind == 5) {
        trig = stringBuf[mark - 2];  // default is $

        rprintf("trig = %c\n\r", trig);
      } else if (ind == 6) {
        frame = (stringBuf[mark - 2] - 48) + (stringBuf[mark - 3] - 48) * 10 +
                (stringBuf[mark - 4] - 48) * 100;
        if (frame > 510) {
          frame = 510;
        }  // up to 510 characters
        rprintf("frame = %d\n\r", frame);
      } else if (ind == 7) {
        ad1_3 = stringBuf[mark - 2];  // default is 'N'
        if (ad1_3 == 'Y') {
          temp2++;
        }
        rprintf("ad1_3 = %c\n\r", ad1_3);
      } else if (ind == 8) {
        ad0_3 = stringBuf[mark - 2];  // default is 'N'
        if (ad0_3 == 'Y') {
          temp2++;
        }
        rprintf("ad0_3 = %c\n\r", ad0_3);
      } else if (ind == 9) {
        ad0_2 = stringBuf[mark - 2];  // default is 'N'
        if (ad0_2 == 'Y') {
          temp2++;
        }
        rprintf("ad0_2 = %c\n\r", ad0_2);
      } else if (ind == 10) {
        ad0_1 = stringBuf[mark - 2];  // default is 'N'
        if (ad0_1 == 'Y') {
          temp2++;
        }
        rprintf("ad0_1 = %c\n\r", ad0_1);
      } else if (ind == 11) {
        ad1_2 = stringBuf[mark - 2];  // default is 'N'
        if (ad1_2 == 'Y') {
          temp2++;
        }
        rprintf("ad1_2 = %c\n\r", ad1_2);
      } else if (ind == 12) {
        ad0_4 = stringBuf[mark - 2];  // default is 'N'
        if (ad0_4 == 'Y') {
          temp2++;
        }
        rprintf("ad0_4 = %c\n\r", ad0_4);
      } else if (ind == 13) {
        ad1_7 = stringBuf[mark - 2];  // default is 'N'
        if (ad1_7 == 'Y') {
          temp2++;
        }
        rprintf("ad1_7 = %c\n\r", ad1_7);
      } else if (ind == 14) {
        ad1_6 = stringBuf[mark - 2];  // default is 'N'
        if (ad1_6 == 'Y') {
          temp2++;
        }
        rprintf("ad1_6 = %c\n\r", ad1_6);
      } else if (ind == 15) {
        safety = stringBuf[mark - 2];  // default is 'Y'
        rprintf("safety = %c\n\r", safety);
      }
    }
  }

  if (safety == 'Y') {
    if ((temp2 == 10) && (freq > 150)) {
      freq = 150;
    } else if ((temp2 == 9) && (freq > 166)) {
      freq = 166;
    } else if ((temp2 == 8) && (freq > 187)) {
      freq = 187;
    } else if ((temp2 == 7) && (freq > 214)) {
      freq = 214;
    } else if ((temp2 == 6) && (freq > 250)) {
      freq = 250;
    } else if ((temp2 == 5) && (freq > 300)) {
      freq = 300;
    } else if ((temp2 == 4) && (freq > 375)) {
      freq = 375;
    } else if ((temp2 == 3) && (freq > 500)) {
      freq = 500;
    } else if ((temp2 == 2) && (freq > 750)) {
      freq = 750;
    } else if ((temp2 == 1) && (freq > 1500)) {
      freq = 1500;
    } else if ((temp2 == 0)) {
      freq = 100;
    }
  }

  if (safety == 'T') {
    test();
  }
}

void local::mode_0()  // Auto UART mode
{
  rprintf("MODE 0\n\r");
  setup_uart0(baud, 1);
  stringSize = buf_size;
  mode_action();
  // rprintf("Exit mode 0\n\r");
}

void local::mode_1() {
  rprintf("MODE 1\n\r");

  setup_uart0(baud, 2);
  stringSize = frame + 2;

  mode_action();
}

void local::mode_2() {
  rprintf("MODE 2\n\r");
  enableIRQ();
  // Timer0  interrupt is an IRQ interrupt
  VICIntSelect &= ~0x00000010;
  // Enable Timer0 interrupt
  VICIntEnable |= 0x00000010;
  // Use slot 2 for UART0 interrupt
  VICVectCntl2 = 0x24;
  // Set the address of ISR for slot 1
  VICVectAddr2 = (unsigned int)MODE2ISR;

  T0TCR = 0x00000002;  // Reset counter and prescaler
  T0MCR = 0x00000003;  // On match reset the counter and generate interrupt
  T0MR0 = 58982400 / freq;

  T0PR = 0x00000000;

  T0TCR = 0x00000001;  // enable timer

  stringSize = 512;
  mode_action();
}

void local::mode_action() {
  int j;
  while (1) {
    if (log_array1 == 1) {
      stat(0, ON);

      if (fat_write_file(handle, (unsigned char*)RX_array1, stringSize) < 0) {
        while (1) {
          stat(0, ON);
          for (j = 0; j < 500000; j++) stat(0, OFF);
          stat(1, ON);
          for (j = 0; j < 500000; j++) stat(1, OFF);
        }
      }

      sd_raw_sync();
      stat(0, OFF);
      log_array1 = 0;
    }

    if (log_array2 == 1) {
      stat(1, ON);

      if (fat_write_file(handle, (unsigned char*)RX_array2, stringSize) < 0) {
        while (1) {
          stat(0, ON);
          for (j = 0; j < 500000; j++) stat(0, OFF);
          stat(1, ON);
          for (j = 0; j < 500000; j++) stat(1, OFF);
        }
      }

      sd_raw_sync();
      stat(1, OFF);
      log_array2 = 0;
    }

    if ((IOPIN0 & 0x00000008) == 0)  // if button pushed, log file & quit
    {
      VICIntEnClr = 0xFFFFFFFF;

      if (RX_in < buf_size) {
        fat_write_file(handle, (unsigned char*)RX_array1, RX_in);
        sd_raw_sync();
      } else if (RX_in >= buf_size) {
        fat_write_file(handle, (unsigned char*)RX_array2, RX_in - buf_size);
        sd_raw_sync();
      }
      while (1) {
        stat(0, ON);
        for (j = 0; j < 500000; j++)
          ;
        stat(0, OFF);
        stat(1, ON);
        for (j = 0; j < 500000; j++)
          ;
        stat(1, OFF);
      }
    }
  }
}

void local::test() {
  rprintf("\n\rLogomatic V2 Test Code:\n\r");
  rprintf(
      "ADC Test will begin in 5 seconds, hit stop button to terminate the "
      "test.\r\n\n");

  delay_ms(5000);

  while ((IOPIN0 & 0x00000008) == 0x00000008) {
    // Get AD1.3
    AD1CR = 0x0020FF08;
    AD_conversion(1);

    // Get AD0.3
    AD0CR = 0x0020FF08;
    AD_conversion(0);

    // Get AD0.2
    AD0CR = 0x0020FF04;
    AD_conversion(0);

    // Get AD0.1
    AD0CR = 0x0020FF02;
    AD_conversion(0);

    // Get AD1.2
    AD1CR = 0x0020FF04;
    AD_conversion(1);

    // Get AD0.4
    AD0CR = 0x0020FF10;
    AD_conversion(0);

    // Get AD1.7
    AD1CR = 0x0020FF80;
    AD_conversion(1);

    // Get AD1.6
    AD1CR = 0x0020FF40;
    AD_conversion(1);

    delay_ms(1000);
    rprintf("\n\r");
  }

  rprintf("\n\rTest complete, locking up...\n\r");
  while (1)
    ;
}

void local::AD_conversion(int regbank) {
  int temp = 0, temp2;

  if (!regbank)  // bank 0
  {
    AD0CR |= 0x01000000;  // start conversion
    while ((temp & 0x80000000) == 0) {
      temp = AD0DR;
    }
    temp &= 0x0000FFC0;
    temp2 = temp / 0x00000040;

    AD0CR = 0x00000000;
  } else  // bank 1
  {
    AD1CR |= 0x01000000;  // start conversion
    while ((temp & 0x80000000) == 0) {
      temp = AD1DR;
    }
    temp &= 0x0000FFC0;
    temp2 = temp / 0x00000040;

    AD1CR = 0x00000000;
  }

  rprintf("%d", temp2);
  rprintf("   ");
}

void local::fat_initialize() {
  if (!sd_raw_init()) {
    rprintf("SD Init Error\n\r");
    while (1)
      ;
  }

  if (openroot()) {
    rprintf("SD OpenRoot Error\n\r");
  }
}
