/******************************************************************************/
/*                                                                            */
/* Copyright Spark Fun Electronics                                            */
/******************************************************************************/

#include "delay.h"

//Short delay
void delay_ms(int count)
{
    int i;
    count *= 10000;
    for (i = 0; i < count; i++)
    {
        asm volatile ("nop");
    }
}
