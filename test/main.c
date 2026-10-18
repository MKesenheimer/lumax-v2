/*
To build use the following gcc statement
(assuming you have libftdi1 installed in the standard location).
gcc -o test -L. -llumax -I. -Wl,-rpath,@loader_path main.c
*/

#include <stdio.h>
#include "lumax.h"

#define NUM_POINTS 1000
#define MAX_CARDS 8   /* matches the library's MAX_DEVICES */

/* Open every connected card, send each one the same 1000-point frame so all
   beams play at the same time, then stop and close them all. */
int main()
 {
  int i, card, NumOfCards, result, TimeToWait, BufferChanged;
  int Handle[MAX_CARDS];
  TLumax_Point Points[NUM_POINTS];

  for (i = 0; i < MAX_CARDS; i++)
   Handle[i] = 0;

  NumOfCards = Lumax_GetPhysicalDevices();
  printf("Number of MiniLumax devices: %i\n", NumOfCards);
  if (NumOfCards > 0)
   {
    /* Build the frame once so every connected card outputs the identical
       content. (Per-point channels: Ch1=X, Ch2=Y, Ch3=R, Ch4=G, Ch5=B,
       Ch6/Ch7/Ch8 extra, TTL.) */
    for (i = 0; i < NUM_POINTS; i++)
     {
      Points[i].Ch1 = i * 65;
      Points[i].Ch2 = (i % 100) * 650;
      Points[i].Ch3 = 100 * 256;
      Points[i].Ch4 = 150 * 256;
      Points[i].Ch5 = 255 * 256;
      Points[i].Ch8 = 0;
      Points[i].Ch6 = 0;
      Points[i].Ch7 = 0;
      //Points[i].TTL = TTL;
     }

    /* 1. Open every card (Handle[card] = 1-based, matching the card index). */
    for (card = 1; card <= NumOfCards && card < MAX_CARDS; card++)
     {
      Handle[card] = Lumax_OpenDevice(card, 0);
      printf("Card %i: Lumax_OpenDevice returned handle: %i\n", card, Handle[card]);
      if (Handle[card] > 0)
       Lumax_SetTTL(Handle[card], 0x55);
     }

    /* 2. Send the same frame to every open card so the beams play together. */
    for (card = 1; card <= NumOfCards && card < MAX_CARDS; card++)
     {
      if (Handle[card] <= 0)
        continue;
      result = Lumax_WaitForBuffer(Handle[card], 0, &TimeToWait, &BufferChanged);
      printf("Card %i: Lumax_WaitForBuffer return code: %i   TimeToWait=%i, BufferChanged=%i\n", card, result, TimeToWait, BufferChanged);
      result = Lumax_SendFrame(Handle[card], Points, NUM_POINTS, 10000, 0, NULL);
      printf("Card %i: Lumax_SendFrame return code: %i\n", card, result);
     }

    /* 3. Stop every open card. */
    for (card = 1; card <= NumOfCards && card < MAX_CARDS; card++)
     {
      if (Handle[card] <= 0)
        continue;
      result = Lumax_WaitForBuffer(Handle[card], 0, &TimeToWait, &BufferChanged);
      printf("Card %i: Lumax_WaitForBuffer return code: %i   TimeToWait=%i, BufferChanged=%i\n", card, result, TimeToWait, BufferChanged);
      result = Lumax_StopFrame(Handle[card]);
      printf("Card %i: Lumax_StopFrame return code: %i\n", card, result);
     }

    /* 4. Close every open card. */
    for (card = 1; card <= NumOfCards && card < MAX_CARDS; card++)
     {
      if (Handle[card] <= 0)
        continue;
      result = Lumax_CloseDevice(Handle[card]);
      printf("Card %i: Lumax_CloseDevice return code: %i\n", card, result);
     }
   }
  return 0;
 }
