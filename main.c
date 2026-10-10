/*
To build use the following gcc statement 
(assuming you have the d2xx library in the /usr/local/lib directory).
gcc -o simple -L. -lftd2xx -lliblumax.so.1.0.1 -Wl,-rpath /usr/local/lib main.c
*/

#include <stdio.h>
#include "lumax.h"


int main()
 {
  int i, NumOfCards, Handle, result, TimeToWait, BufferChanged;
  unsigned char Buffer[1000];
  TLumax_Point Points[1000];
  printf("**********************************************************\n");
  NumOfCards = Lumax_GetPhysicalDevices();
  printf("Number of MiniLumax devices: %i\n", NumOfCards);
  if (NumOfCards > 0)
   {
    Handle = Lumax_OpenDevice(1, 0);
    printf("Lumax_OpenDevice returned handle: %i\n", Handle);
    if (Handle > 0)
     {
      result = Lumax_SetTTL(Handle, 0x55);
      printf("Lumax_SetTTL return code: %i\n", result);
      result = Lumax_WaitForBuffer(Handle, 0, &TimeToWait, &BufferChanged);
      printf("Lumax_WaitForBuffer return code: %i   TimeToWait=%i, BufferChanged=%i\n", result, TimeToWait, BufferChanged);
      for (i = 0; i < 1000; i++)
       {
        Points[i].Ch1 = i * 65;
        Points[i].Ch2 = (i % 100) * 650;
        Points[i].Ch3 = 100 * 256;
        Points[i].Ch4 = 150 * 256;
        Points[i].Ch5 = 255 * 256;
        Points[i].Ch8 = 0;
        Points[i].Ch6 = 0;
        Points[i].Ch7 = 0;
//        Points[i].TTL = TTL;
       }
      result = Lumax_SendFrame(Handle, Points, 1000, 10000, 0, NULL);
      printf("Lumax_SendFrame return code: %i\n", result);
      result = Lumax_WaitForBuffer(Handle, 0, &TimeToWait, &BufferChanged);
      printf("Lumax_WaitForBuffer return code: %i   TimeToWait=%i, BufferChanged=%i\n", result, TimeToWait, BufferChanged);
      result = Lumax_CloseDevice(Handle);
      printf("Lumax_CloseDevice return code: %i\n", result);
     }
   }
  return 0;
 }
