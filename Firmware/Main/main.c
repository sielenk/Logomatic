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
#include "serial.h"
#include "rprintf.h"

// Needed for main function calls
#include "main_msc.h"
#include "fat.h"
#include "armVIC.h"
#include "itoa.h"
#include "rootdir.h"
#include "sd_raw.h"
#include "string_printf.h"
#include "delay.h"

/*******************************************************
 * 		     Global Variables
 ******************************************************/

enum COLOR { RED = 0x00000004, GREEN = 0x00000800 };
enum LED_STATE { LED_OFF = 0, LED_ON = 1 };

#define BUF_SIZE 512

char RX_array1[BUF_SIZE];
char RX_array2[BUF_SIZE];
char log_array1 = 0;
char log_array2 = 0;
short RX_in = 0;
char get_frame = 0;

signed int stringSize;
struct fat_file_struct* handle;
struct fat_file_struct* fd;
char stringBuf[256];

// Default Settings
static char mode = 0;
static char asc = 'N';
static int baud = 9600;
static int freq = 100;
static char trig = '$';
static short frame = 100;
static char ad1_7 = 'N';
static char ad1_6 = 'N';
static char ad1_3 = 'N';
static char ad1_2 = 'N';
static char ad0_4 = 'N';
static char ad0_3 = 'N';
static char ad0_2 = 'N';
static char ad0_1 = 'N';

/*******************************************************
 * 		 Function Declarations
 ******************************************************/

void Initialize(void);

void setup_uart0(int newbaud, char want_ints);

void mode_0(void);
void mode_1(void);
void mode_2(void);
void mode_action(void);

void Log_init(void);
void test(void);
void stat(enum COLOR statnum, enum LED_STATE onoff);
void AD_conversion(int regbank, int pin);

void feed(void);

static void UART0ISR(void);    //__attribute__ ((interrupt("IRQ")));
static void UART0ISR_2(void);  //__attribute__ ((interrupt("IRQ")));
static void MODE2ISR(void);    //__attribute__ ((interrupt("IRQ")));

void FIQ_Routine(void) __attribute__((interrupt("FIQ")));
void SWI_Routine(void) __attribute__((interrupt("SWI")));
void UNDEF_Routine(void) __attribute__((interrupt("UNDEF")));

void fat_initialize(void);

/*******************************************************
 * 		     	MAIN
 ******************************************************/

int main(void) {
  int i;
  char name[32];
  int count = 0;

  enableFIQ();

  Initialize();

  // setup_uart0(9600, 0);

  fat_initialize();

  // Flash Status Lights
  for (i = 0; i < 5; i++) {
    stat(RED, LED_ON);
    delay_ms(50);
    stat(RED, LED_OFF);
    stat(GREEN, LED_ON);
    delay_ms(50);
    stat(GREEN, LED_OFF);
  }

  Log_init();

  count++;
  string_printf(name, "LOG%02d.txt", count);
  while (root_file_exists(name)) {
    count++;
    if (count == 250) {
      rprintf("Too Many Logs!\n\r");
      while (1) {
        stat(RED, LED_ON);
        stat(GREEN, LED_ON);
        delay_ms(1000);
        stat(RED, LED_OFF);
        stat(GREEN, LED_OFF);
        delay_ms(1000);
      }
    }
    string_printf(name, "LOG%02d.txt", count);
  }

  handle = root_open_new(name);

  sd_raw_sync();

  if (mode == 0) {
    mode_0();
  } else if (mode == 1) {
    mode_1();
  } else if (mode == 2) {
    mode_2();
  }

  return 0;
}

/*******************************************************
 * 		     Initialize
 ******************************************************/

#define PLOCK 0x400

void Initialize(void) {
  rprintf_devopen(putc_serial0);

  PINSEL0 = 0xCF351505;
  PINSEL1 = 0x15441801;
  IODIR0 |= 0x00000884;
  IOSET0 = 0x00000080;

  S0SPCR = 0x08;  // SPI clk to be pclk/8
  S0SPCR = 0x30;  // master, msb, first clk edge, active high, no ints
}

void feed(void) {
  PLLFEED = 0xAA;
  PLLFEED = 0x55;
}

static void UART0ISR(void) {
  char temp;

  if (RX_in < BUF_SIZE) {
    RX_array1[RX_in] = U0RBR;

    RX_in++;

    if (RX_in == BUF_SIZE) {
      log_array1 = 1;
    }
  } else if (RX_in >= BUF_SIZE) {
    RX_array2[RX_in - BUF_SIZE] = U0RBR;
    RX_in++;

    if (RX_in == 2 * BUF_SIZE) {
      log_array2 = 1;
      RX_in = 0;
    }
  }

  temp = U0IIR;  // Have to read this to clear the interrupt

  VICVectAddr = 0;

  (void)temp;
}

static void UART0ISR_2(void) {
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

static int pushValue(char* q, int ind, int value) {
  if (asc == 'Y') {  // ASCII
    // replace the last NUL with a TAB delimiter
    if (ind > 0) {
      q[ind++] = '\t';
    }
    // itoa returns the number of bytes written excluding
    // trailing '\0'
    ind += itoa(value, 10, q + ind);
  } else if (asc == 'N') {  // binary
    q[ind++] = value >> 8;
    q[ind++] = value;
  }

  return ind;
}

static int getSample(int bank, int pin) {
  volatile unsigned long* const ADxCR = !bank ? &AD0CR : &AD1CR;
  volatile unsigned long* const ADxDR = !bank ? &AD0DR : &AD1DR;
  int value = 0;

  *ADxCR = 0x00020FF00 | (1 << pin);
  *ADxCR |= 0x01000000;  // start conversion
  while ((value & 0x80000000) == 0) {
    value = *ADxDR;
  }
  *ADxCR = 0x00000000;

  // The upper ten of the lower sixteen bits of 'value' are the
  // result. The result itself is unsigned. Hence a cast to
  // 'unsigned short' yields the result with six bits of
  // noise. Those are removed by the following shift operation.
  return (unsigned short)value >> 6;
}

static int sampleAndWrite(char* q, int ind, int bank, int pin,
                          char ad_bank_pin) {
  if (ad_bank_pin == 'Y') {
    return pushValue(q, ind, getSample(bank, pin));
  } else {
    return ind;
  }
}

static void MODE2ISR(void) {
  int ind = 0;
  int j;
  char q[50];

  T0IR = 1;  // reset TMR0 interrupt

  for (j = 0; j < 50; j++) {
    q[j] = 0;
  }

#define SAMPLE(BANK, PIN) \
  ind = sampleAndWrite(q, ind, BANK, PIN, ad##BANK##_##PIN)
  SAMPLE(1, 3);
  SAMPLE(0, 3);
  SAMPLE(0, 2);
  SAMPLE(0, 1);
  SAMPLE(1, 2);
  SAMPLE(0, 4);
  SAMPLE(1, 7);
  SAMPLE(1, 6);
#undef SAMPLE

  for (j = 0; j < ind; j++) {
    if (RX_in < BUF_SIZE) {
      RX_array1[RX_in] = q[j];
      RX_in++;

      if (RX_in == BUF_SIZE) {
        log_array1 = 1;
      }
    } else if (RX_in >= BUF_SIZE) {
      RX_array2[RX_in - BUF_SIZE] = q[j];
      RX_in++;

      if (RX_in == 2 * BUF_SIZE) {
        log_array2 = 1;
        RX_in = 0;
      }
    }
  }
  if (RX_in < BUF_SIZE) {
    if (asc == 'N') {
      RX_array1[RX_in] = '$';
    } else if (asc == 'Y') {
      RX_array1[RX_in] = 13;
    }
    RX_in++;

    if (RX_in == BUF_SIZE) {
      log_array1 = 1;
    }
  } else if (RX_in >= BUF_SIZE) {
    if (asc == 'N') {
      RX_array2[RX_in - BUF_SIZE] = '$';
    } else if (asc == 'Y') {
      RX_array2[RX_in - BUF_SIZE] = 13;
    }
    RX_in++;

    if (RX_in == 2 * BUF_SIZE) {
      log_array2 = 1;
      RX_in = 0;
    }
  }
  if (RX_in < BUF_SIZE) {
    if (asc == 'N') {
      RX_array1[RX_in] = '$';
    } else if (asc == 'Y') {
      RX_array1[RX_in] = 10;
    }
    RX_in++;

    if (RX_in == BUF_SIZE) {
      log_array1 = 1;
    }
  } else if (RX_in >= BUF_SIZE) {
    if (asc == 'N') {
      RX_array2[RX_in - BUF_SIZE] = '$';
    } else if (asc == 'Y') {
      RX_array2[RX_in - BUF_SIZE] = 10;
    }
    RX_in++;

    if (RX_in == 2 * BUF_SIZE) {
      log_array2 = 1;
      RX_in = 0;
    }
  }

  VICVectAddr = 0;
}

void FIQ_Routine(void) {
  char a;
  int j;

  stat(RED, LED_ON);
  delay_ms(500);
  stat(RED, LED_OFF);
  a = U0RBR;

  a = U0IIR;  // have to read this to clear the interrupt

  (void)a;
}

void SWI_Routine(void) {
  while (1)
    ;
}

void UNDEF_Routine(void) {
  stat(RED, LED_ON);
}

void setup_uart0(int newbaud, char want_ints) {
  baud = newbaud;
  U0LCR = 0x83;  // 8 bits, no parity, 1 stop bit, DLAB = 1

  if (baud == 1200) {
    U0DLM = 0x0C;
    U0DLL = 0x00;
  } else if (baud == 2400) {
    U0DLM = 0x06;
    U0DLL = 0x00;
  } else if (baud == 4800) {
    U0DLM = 0x03;
    U0DLL = 0x00;
  } else if (baud == 9600) {
    U0DLM = 0x01;
    U0DLL = 0x80;
  } else if (baud == 19200) {
    U0DLM = 0x00;
    U0DLL = 0xC0;
  } else if (baud == 38400) {
    U0DLM = 0x00;
    U0DLL = 0x60;
  } else if (baud == 57600) {
    U0DLM = 0x00;
    U0DLL = 0x40;
  } else if (baud == 115200) {
    U0DLM = 0x00;
    U0DLL = 0x20;
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

void stat(enum COLOR color, enum LED_STATE state) {
  if (state == LED_ON) {
    IOCLR0 = color;
  } else {
    IOSET0 = color;
  }  // Off
}

void Log_init(void) {
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
        stat(RED, LED_ON);
        delay_ms(50);
        stat(RED, LED_OFF);
        stat(GREEN, LED_ON);
        delay_ms(50);
        stat(GREEN, LED_OFF);
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
        if (stringBuf[mark - 2] == '1') {
          baud = 1200;
        } else if (stringBuf[mark - 2] == '2') {
          baud = 2400;
        } else if (stringBuf[mark - 2] == '3') {
          baud = 4800;
        } else if (stringBuf[mark - 2] == '4') {
          baud = 9600;
        } else if (stringBuf[mark - 2] == '5') {
          baud = 19200;
        } else if (stringBuf[mark - 2] == '6') {
          baud = 38400;
        } else if (stringBuf[mark - 2] == '7') {
          baud = 57600;
        } else if (stringBuf[mark - 2] == '8') {
          baud = 115200;
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

void mode_0(void)  // Auto UART mode
{
  rprintf("MODE 0\n\r");
  setup_uart0(baud, 1);
  stringSize = BUF_SIZE;
  mode_action();
  // rprintf("Exit mode 0\n\r");
}

void mode_1(void) {
  rprintf("MODE 1\n\r");

  setup_uart0(baud, 2);
  stringSize = frame + 2;

  mode_action();
}

void mode_2(void) {
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

  stringSize = BUF_SIZE;
  mode_action();
}

void mode_action(void) {
  int j;
  while (1) {
    if (log_array1 == 1) {
      stat(RED, LED_ON);

      if (fat_write_file(handle, (unsigned char*)RX_array1, stringSize) < 0) {
        rprintf("failure 1\n\r");
        while (1) {
          stat(RED, LED_ON);
          delay_ms(50);
          stat(RED, LED_OFF);
          stat(GREEN, LED_ON);
          delay_ms(50);
          stat(GREEN, LED_OFF);
        }
      }

      sd_raw_sync();
      stat(RED, LED_OFF);
      log_array1 = 0;
    }

    if (log_array2 == 1) {
      stat(GREEN, LED_ON);

      if (fat_write_file(handle, (unsigned char*)RX_array2, stringSize) < 0) {
        rprintf("failure 2\n\r");
        while (1) {
          stat(RED, LED_ON);
          delay_ms(50);
          stat(RED, LED_OFF);
          stat(GREEN, LED_ON);
          delay_ms(50);
          stat(GREEN, LED_OFF);
        }
      }

      sd_raw_sync();
      stat(GREEN, LED_OFF);
      log_array2 = 0;
    }

    if ((IOPIN0 & 0x00000008) == 0)  // if button pushed, log file & quit
    {
      VICIntEnClr = 0xFFFFFFFF;

      if (RX_in < BUF_SIZE) {
        fat_write_file(handle, (unsigned char*)RX_array1, RX_in);
        sd_raw_sync();
      } else if (RX_in >= BUF_SIZE) {
        fat_write_file(handle, (unsigned char*)RX_array2, RX_in - BUF_SIZE);
        sd_raw_sync();
      }

      rprintf("stopped\n\r");
      while (1) {
        stat(RED, LED_ON);
        delay_ms(50);
        stat(RED, LED_OFF);
        stat(GREEN, LED_ON);
        delay_ms(50);
        stat(GREEN, LED_OFF);
      }
    }
  }
}

void test(void) {
  rprintf("\n\rLogomatic V2 Test Code:\n\r");
  rprintf(
      "ADC Test will begin in 5 seconds, hit stop button to terminate the "
      "test.\r\n\n");

  delay_ms(5000);

  while ((IOPIN0 & 0x00000008) == 0x00000008) {
    AD_conversion(1, 3);
    AD_conversion(0, 3);
    AD_conversion(0, 2);
    AD_conversion(0, 1);
    AD_conversion(1, 2);
    AD_conversion(0, 4);
    AD_conversion(1, 7);
    AD_conversion(1, 6);

    delay_ms(1000);
    rprintf("\n\r");
  }

  rprintf("\n\rTest complete, locking up...\n\r");
  while (1)
    ;
}

void AD_conversion(int regbank, int pin) {
  rprintf("%d", getSample(regbank, pin));
  rprintf("   ");
}

void fat_initialize(void) {
  if (!sd_raw_init()) {
    rprintf("SD Init Error\n\r");
    while (1)
      ;
  }

  if (openroot()) {
    rprintf("SD OpenRoot Error\n\r");
  }
}
