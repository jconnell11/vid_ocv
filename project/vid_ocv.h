// vid_ocv.h : simple reading and displaying of video using OpenCV
//
// Written by Jonathan H. Connell, jconnell@alum.mit.edu
//
///////////////////////////////////////////////////////////////////////////
//
// Copyright 2024-2026 Etaoin Systems
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

// Modified for faster background framegrabbing based on jhcMpiCam class

// NOTE: needs opencv_world4100.dll (any version)

#pragma once

#include <stddef.h>           // for NULL


#ifdef __linux__
  #define DEXP                // nothing special needed for Linux shared lib
#else

  // function declarations 
  #ifdef VIDOCV_EXPORTS
    #define DEXP __declspec(dllexport)
  #else
    #define DEXP __declspec(dllimport)
  #endif

  // link to library stub
  #ifndef VIDOCV_EXPORTS
    #pragma comment(lib, "vid_ocv.lib")
  #endif

#endif


///////////////////////////////////////////////////////////////////////////
//                           Video Functions                             //
///////////////////////////////////////////////////////////////////////////

//= Tries to open a video source (file or stream) and grabs a test frame.
// can optionally flip all images vertically so top becomes bottom
// only a single source can be active at a time with this DLL
// returns positive if successful, 0 or negative for failure

extern "C" DEXP int ocv_open (const char *fname =NULL, int vflip =0);


//= Tries to open a local camera for input and grabs a test frame.
// use unit = -1 to get first working camera
// can optionally flip all images vertically so top becomes bottom
// only a single source can be active at a time with this DLL
// returns positive if successful, 0 or negative for failure

extern "C" DEXP int ocv_cam (int unit =-1, int vflip =0);


//= Tells whether more frames are available from the source.
// returns 1 if still running, 0 if stopped, -1 if never opened

extern "C" DEXP int ocv_live ();


//= Binds dimensions and framerate of currently active video source.
// returns 1 if info valid (even if stopped), 0 if no current source 

extern "C" DEXP int ocv_info (int& iw, int& ih, double& fps);


//= Set geometric manipulations to perform on raw image.
// de-warped version will have optical center in middle of image
//   r2f = r^2 lens radial distortion x 10^6 (pixel coords)
//   r4f = r^4 lens radial distortion x 10^12 (pixel coords)
//   mag = overall magnification after correction
//   rot = rotation of image around final center (degs)
//   cx  = lens center x coordinate (defaults to mid-x)
//   cy  = lens center y coordinate (defaults to mid-y)
// needs to know image size from ocv_open() before building transform tables
// NOTE: if no warp specified then image passes through with no correction
 
extern "C" DEXP void ocv_warp (double r2f, double r4f =0.0, double r6f =0.0, double flen =553.0,
                               double mag =1.0, double rot =1.0, double cx =0.0, double cy =0.0);
                               

//= Bind filled framebuffer for next image to supplied pointer.
// images are left-to-right, bottom-up, with BGR color order
// can optionally block until brand new image becomes available
// always gives most recently grabbed frame (not next one) hence some drops
// returns frame count if buffer is new, 0 if not ready, neg for error

extern "C" DEXP int ocv_get (const unsigned char **buf, int block =0);


//= Reports actual framerate since video source was started.
// returns number of frames delivered

extern "C" DEXP int ocv_rate (double& fps);


//= Disconnect from current video source (automatically called on exit).
// always gives most recently grabbed frame (not next one) hence some drops
// returns number of frames produced that were not consumed

extern "C" DEXP int ocv_done ();


///////////////////////////////////////////////////////////////////////////
//                           Display Functions                           //
///////////////////////////////////////////////////////////////////////////

//= Create a display window with given title and corner position.
// win is between 0 and 5 inclusive, titles must be unique
// if rec > 0 then saves images in "rec/<title>.mp4" ("rec" dir must exist)
// returns 1 if successful, 0 or negative for problem

extern "C" DEXP int ocv_win (int win, const char *title =NULL, 
                             int cx =-1, int cy =0, int rec =0);


//= Send an image to some window for display (must call ocv_show() later).
// buffer is left-to-right, bottom-up, BGR color order and size iw x ih
// if mark > 0 then saves as "rec/<title>_<mark>.bmp" ("rec" dir must exist)
// returns 1 if successful, 0 or negative for problem

extern "C" DEXP int ocv_queue (int win, const unsigned char *buf, 
                               int iw =640, int ih =480, int mark =0);


//= Update all display windows with queued buffers (blocks for 1 ms).

extern "C" DEXP void ocv_show ();


//= Checks a particular window for position of most recent mouse click.
// coords wrt to displayed buffer, y is TOP DOWN, clears status during call
// returns 0 if nothing, 1 for left button, 3 for right button

extern "C" DEXP int ocv_click (int win, int& x, int& y);
