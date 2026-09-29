// vid_test.cpp : speed test of spio_win.dll video capture and display
//
// Written by Jonathan H. Connell, jconnell@alum.mit.edu
//
///////////////////////////////////////////////////////////////////////////
//
// Copyright 2026 Etaoin Systems
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// 
///////////////////////////////////////////////////////////////////////////

#ifndef __linux__
  #include <windows.h>                 // needed for Sleep
  #include <stdio.h>
  #pragma comment(lib, "winmm.lib")    // for timeGetTime
#else
  #include <time.h>
  #include "jhc_str_s.h"
#endif

#include "jhc_conio.h"

#include "vid_ocv.h"


//= Speed test of ocv_vid.dll video capture and display.
// no args = ESP32Cam wifi streaming with warp from Baijiu robot
// else argument is camera unit number (no warping applied)
// Baijiu: about 24 fps @ 6', 18 fps @ 13' (no antenna -> EdiMax)
// NOTE: needs ocv_vid.dll and opencv_world4100.dll!

int main (int argc, char *argv[])
{
  char ipname[80] = "http://192.168.0.200:81/stream";
  const unsigned char *buf = NULL;
  double fps;
  int rc, unit, iw, ih, drop, cnt = 0;

  // connect to camera
  if (argc > 1)
  {
    unit = atoi(argv[1]);
    printf("Opening camera %d ... ", unit);
    rc = ocv_cam(unit, 1);
  }
  else
  {
    printf("Opening %s ... ", ipname);
    rc = ocv_open(ipname, 1);
  }
  if (rc <= 0)
  {
    printf("\n  Failed to open video source!\n");
    return 0;
  }
  ocv_info(iw, ih, fps);
  printf("%3.1f fps\n", fps);

  // geometric correct (but not for camera unit number) and display
  if (argc < 2)        
    ocv_warp(0.14, -0.13, 0.024, 219, 1, 0, 313, 242);   
  ocv_win(0, "Camera View", 1100, 0);     
//  ocv_win(0, "Camera View", 1100, 0, 1);         // save as video

  // continuously framegrab
  printf("Streaming video (hit any key to stop) ...\n");
  while (!_kbhit())
  {
    if ((cnt = ocv_get(&buf, 1)) <= 0)           // blocks
    {
      printf("Video connection lost!\n");
      break;
    }
    ocv_queue(0, buf, iw, ih);
    ocv_show(); 
    printf("\r  %d ", cnt);
    fflush(stdout);
  }
  printf("\r");
//  ocv_queue(0, buf, iw, ih, 1);                  // save last image

  // stop video and report speed
  drop = ocv_done();
  ocv_rate(fps);
  if (cnt > 0)
    printf("  %d frames in %3.1f secs = %3.1f fps (%d dropped)\n", cnt, cnt / fps, fps, drop);
  else
    printf("  0 frames in 0.0 secs = 0.0 fps\n");

  // keep terminal window visible
  while (_kbhit())
    _getch();
  printf("Hit any key to exit ...\n");
  _getch();
  _kbdone();                 // for Linux
  return 1;
}

