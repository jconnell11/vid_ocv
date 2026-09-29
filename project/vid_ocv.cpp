// vid_ocv.cpp : simple reading and displaying of video using OpenCV 
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

#ifndef __linux__
  #include <windows.h>
  #include <stdio.h>
  #define _USE_MATH_DEFINES
  #include <math.h>
  #pragma comment(lib, "opencv_world4100.lib") 
#else
  #include <time.h>
  #include "jhc_str_s.h"

  static void Sleep (int ms)
  {
    timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = 1000000 * (ms % 1000);
    nanosleep(&ts, NULL); 
  }
#endif

#include "jhc_pthread.h"

#include "opencv2/opencv.hpp"                 

#include "vid_ocv.h"


///////////////////////////////////////////////////////////////////////////
//                          Global Variables                             //
///////////////////////////////////////////////////////////////////////////

//= Video capture instance for frame grabbing.

static cv::VideoCapture vcap;
static cv::Mat raw;
static cv::TickMeter tick;
static int invert, cnt, drop, ok = -1;


//= Background receiver and coordination.

static pthread_t hoover;
static pthread_mutex_t data;
static int run = 0;     


//= Color images and status.

static cv::Mat c0, c1, c2;
static cv::Mat *fill, *done, *lock;
static int fresh;


//= Cached resampling positions and interpolation factors.

static unsigned long *base = NULL;
static unsigned short *mix = NULL;
static int npel = 0;


//= Display window names and corner positions.

static char name[6][40] = {"", "", "", "", "", ""};
static int wx[6], wy[6];


//= Mouse click information for each window.

static int id[6], but[6], mx[6], my[6];


//= Video output stream for each window and whether to write.

static cv::VideoWriter writer[6];
static int save[6] = {0, 0, 0, 0, 0, 0};


///////////////////////////////////////////////////////////////////////////
//                             Initialization                            //
///////////////////////////////////////////////////////////////////////////

#ifndef __linux__

  //= Clean up on exit.

  BOOL APIENTRY DllMain (HANDLE hModule,
                         DWORD ul_reason_for_call, 
                         LPVOID lpReserved)
  {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
      pthread_mutex_init(&data, NULL);
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
      ocv_done();
      pthread_mutex_destroy(&data);
    }
    return TRUE;
  }

#endif


///////////////////////////////////////////////////////////////////////////
//                        Background Acquisition                         //
///////////////////////////////////////////////////////////////////////////

//= Apply geometric tranform to image using pre-computed tables.
// assumes images are always color (3 bytes per pixel)
// returns 1 if successful, 0 or negative for problem
// NOTE: "base" and "mix" arrays set up by call to ocv_warp()

static int fixup (unsigned char *dest, const unsigned char *src)
{
  cv::Size sz = raw.size(); 
  int iw = sz.width, ih = sz.height, ln = 3 * iw;
  int x, y, fx, fy, cfx, cfy, lo, hi, val;
  const unsigned char *bot, *top;
  const unsigned long *b = base;
  const unsigned short *m = mix;
  unsigned char *d = dest;

  // make sure resonable transform exists
  if ((base == NULL) || (mix == NULL) || (npel != (int) raw.total()))
    return 0; 

  // see if integer sampling position is valid
  for (y = 0; y < ih; y++)
    for (x = 0; x < iw; x++, d += 3, b++, m++)
    {
      // outside original -> black
      if (*b == 0xFFFFFFFF)
      {
        d[0] = 0;
        d[1] = 0;
        d[2] = 0;
        continue;
      }

      // base corner of pixel quartet and interpolation coefficients 
      bot = src + (*b);
      top = bot + ln;
      fx = (*m) >> 8;
      fy = (*m) & 0xFF;
      cfx = 256 - fx;
      cfy = 256 - fy;
  
      // interpolate blue pixel
      lo  = cfx * bot[0] + fx * bot[3];
      hi  = cfx * top[0] + fx * top[3];
      val = cfy * lo + fy * hi;
      d[0] = (unsigned char)(val >> 16);

      // interpolate green pixel
      lo  = cfx * bot[1] + fx * bot[4];
      hi  = cfx * top[1] + fx * top[4];
      val = cfy * lo + fy * hi;
      d[1] = (unsigned char)(val >> 16);

      // interpolate red pixel
      lo  = cfx * bot[2] + fx * bot[5];
      hi  = cfx * top[2] + fx * top[5];
      val = cfy * lo + fy * hi;
      d[2] = (unsigned char)(val >> 16);
    }
  return 1;
}


//= Continually receive frames from camera into best open buffer image.

static pthread_ret grab_loop (void *arg)
{
  cv::Mat bot;
  const unsigned char *src;

  // initialize performance metrics
  tick.start();
  drop = 0;
  while (run > 0) 
  {
    // attempt to read next frame (blocks)
    if (!vcap.read(raw))
      break;

    // OpenCV images are top-down
    src = raw.data;
    if (invert > 0)
    {
      cv::flip(raw, bot, 0);
      src = bot.data;
    }

    // apply geometric transform (if any)
    if (fixup(fill->data, src) <= 0)
      memcpy(fill->data, src, 3 * raw.total());  

    // shuffle output images
    pthread_mutex_lock(&data);
    done = fill;                                 // most recent complete
    fresh += 1;
    if (fill == &c0)
      fill = ((lock != &c1) ? &c1 : &c2);
    else if (fill == &c1)
      fill = ((lock != &c0) ? &c0 : &c2);
    else                                         // lock == c2             
      fill = ((lock != &c0) ? &c0 : &c1);
    drop++;                                      // frame produced
    pthread_mutex_unlock(&data);
  }
  ok = 0;                                        // stream ended
  return NULL;
}


///////////////////////////////////////////////////////////////////////////
//                           Video Functions                             //
///////////////////////////////////////////////////////////////////////////

//= Common part of opening a named file/stream or physical camera. 

static int fg_init (int vflip)
{ 
  // try reading a frame then resize buffer images (for memcpy)
  if (!vcap.read(raw))      
    return 0;
  c0.create(raw.size(), raw.type());
  c1.create(raw.size(), raw.type());
  c2.create(raw.size(), raw.type());

  // initialize rotating buffers 
  fill = &c0;
  done = NULL;
  lock = NULL;
  fresh = 0;     

  // launch receiver and pre-processor thread
  invert = vflip;
  run = 1;
  pthread_create(&hoover, NULL, grab_loop, NULL);
  ok = 1;
  cnt = 0;
  return 1;
}


//= Tries to open a video source (file or stream) and grabs a test frame.
// can optionally flip all images vertically so top becomes bottom
// only a single source can be active at a time with this DLL
// returns positive if successful, 0 or negative for failure

extern "C" DEXP int ocv_open (const char *fname, int vflip)
{
  int unit;  

  ok = 0;
  if ((fname == NULL) || (*fname == '\0'))
    return -2;
  if (sscanf_s(fname, "%d", &unit) == 1)         // fname = "1"
    return ocv_cam(unit, vflip);
  if (!vcap.open(fname))
    return -1;
  return fg_init(vflip);
}
 

//= Tries to open a local camera for input and grabs a test frame.
// use unit = -1 to get first working camera
// can optionally flip all images vertically so top becomes bottom
// only a single source can be active at a time with this DLL
// returns positive if successful, 0 or negative for failure

extern "C" DEXP int ocv_cam (int unit, int vflip)
{
  ok = 0;
#ifndef __linux__
  if (!vcap.open(unit))                    // no cv::CAP_V4L2 for Windows
    return -1;
#else
  if (!vcap.open(unit, cv::CAP_V4L2))      // in preference to GStreamer
    return -1;
#endif
  return fg_init(vflip);
}


//= Tells whether more frames are available from the source.
// returns 1 if still running, 0 if stopped, -1 if never opened

extern "C" DEXP int ocv_live ()
{
  return ok;
}


//= Binds dimensions and framerate of currently active video source.
// returns 1 if info valid (even if stopped), 0 if no current source 

extern "C" DEXP int ocv_info (int& iw, int& ih, double& fps)
{
  cv::Size sz = raw.size();

  if (!vcap.isOpened())
    return 0;
  iw = sz.width;
  ih = sz.height;
  fps = vcap.get(cv::CAP_PROP_FPS);
  return 1;
}


//= Set geometric manipulations to perform on raw image.
// de-warped version will have optical center in middle of image
//   k1   = r^2 lens radial distortion (wrt flen)
//   k2   = r^4 lens radial distortion (wrt flen)
//   k3   = r^6 lens radial distortion (wrt flen)
//   flen = focal length (both x and y, in pels)
//   mag  = overall magnification after correction
//   rot  = rotation of image around final center (degs)
//   cx   = lens center x coordinate (defaults to mid-x)
//   cy   = lens center y coordinate (defaults to mid-y)
// needs to know image size from ocv_open() before building transform tables
// NOTE: if no warp specified then image passes through with no correction
 
extern "C" DEXP void ocv_warp (double k1, double k2, double k3, double flen, 
                               double mag, double rot, double cx, double cy)
{
  cv::Size sz = raw.size(); 
  int iw = sz.width, ih = sz.height, xlim = iw - 1, ylim = ih - 1, ln = 3 * iw;
  int x, y, ix, iy, fx, fy;
  double sc = 1.0 / mag, norm2 = 1.0 / (flen * flen), x0 = 0.5 * xlim, y0 = 0.5 * ylim;
  double rads = M_PI * rot / 180.0, c = cos(rads), s = sin(rads);
  double dx0, dy0, dx, dy, dy2, r2, warp, wx, wy;
  unsigned long *b;
  unsigned short *m;

  // use image center if camera center not specified
  if ((cx <= 0.0) || (cy <= 0.0))
  {
    cx = x0;
    cy = y0;  
  }

  // get rid of any old transform
  delete [] mix;
  delete [] base;
  mix  = NULL;
  base = NULL;
  npel = 0;

  // make new cached value arrays if needed (and possible)
  if ((mag <= 0.0) || (iw <= 0) || (ih <= 0) ||
      ((mag == 1.0) && (k1 == 0.0) && (k2 == 0.0) && (k3 == 0.0)))
    return;
  npel = iw * ih;
  base = new unsigned long [4 * npel];
  mix  = new unsigned short [2 * npel];

  // build transform lookup tables "base" and "mix" for fixup()
  b = base;
  m = mix;
  for (y = 0; y < ih; y++)
  {
    // get central offset adjusted for pixel aspect ratio
    dy0 = sc * (y - y0);
    dy2 = dy0 * dy0;
    for (x = 0; x < iw; x++, b++, m++)
    {
      // compute radial offset from center
      dx0 = sc * (x - x0);
      r2 = norm2 * (dx0 * dx0 + dy2);

      // apply rotation correction
      dx = c * dx0 - s * dy0;
      dy = s * dx0 + c * dy0;

      // determine lens warped coordinates
      warp = 1.0 + (k1 + (k2 + k3 * r2) * r2) * r2;
      wx = cx + warp * dx;
      wy = cy + warp * dy;

      // check for valid input pixel location
      if ((wx < 0.0) || (wx >= xlim) || (wy < 0.0) || (wy >= ylim))
      {
        *b = 0xFFFFFFFF;
        continue;
      }

      // get integer part of color sampling location
      ix = (int) wx;
      iy = (int) wy;
      *b = (unsigned long)(iy * ln + 3 * ix);

      // save fractional interpolation coefficients
      fx = (int)(256.0 * (wx - ix) + 0.5);
      fx = ((fx <= 255) ? fx : 255); 
      fy = (int)(256.0 * (wy - iy) + 0.5);
      fy = ((fy <= 255) ? fy : 255); 
      *m = (unsigned short)((fx << 8) | fy);
    }
  }
}


//= Bind filled framebuffer for next image to supplied pointer.
// images are left-to-right, bottom-up, with BGR color order
// can optionally block until brand new image becomes available
// always gives most recently grabbed frame (not next one) hence some drops
// returns frame count if buffer is new, 0 if not ready, neg for error

extern "C" DEXP int ocv_get (const unsigned char **buf, int block)
{
  int wait = 0;

  // check if source is operational and new frame is ready
  if (ok <= 0)  
    return -2;
  while (fresh <= 0)
  {
    if (block <= 0)                    // return immediately
      return 0;
    if (wait++ > 500)                  // barf after 0.5 sec
      return -1;
    Sleep(1);                          // 1 ms loop
  }

  // swap buffers to be sure output pointer remains valid
  pthread_mutex_lock(&data);
  lock = done;                         // mark as in-use
  fresh = 0;
  drop--;                              // frame consumed
  pthread_mutex_unlock(&data);
  *buf = lock->data;
  return ++cnt;                        // frame delivered
}


//= Reports actual framerate since video source was started.
// returns number of frames delivered

extern "C" DEXP int ocv_rate (double& fps)
{
  double secs = tick.getTimeSec();

  fps = ((secs <= 0.0) ? 0.0 : (cnt / secs));
  return cnt;
}


//= Regenerate video for some display window using actual framerate.

static void transcode (int win)
{
  char rname[80], vname[80];
  double fps, secs = tick.getTimeSec(); 
  int iw, ih;

  // finalize temporary video then get frame rate to use
  writer[win].release();
  if (secs <= 0.0)
    return;
  fps = cnt / secs;

  // re-open temporary video to read each frame
  sprintf_s(rname, "rec/raw%d.avi", win);
  if (!vcap.open(rname))
    return;
  iw = (int) vcap.get(cv::CAP_PROP_FRAME_WIDTH);
  ih = (int) vcap.get(cv::CAP_PROP_FRAME_HEIGHT);

  // copy each frame to final video with correct rate
  sprintf_s(vname, "rec/%s.mp4", name[win]);
  remove(vname);
  if (writer[win].open(vname, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), fps, cv::Size(iw, ih), true))
  {
    printf("Transcoding \"%s\" ... ", vname);
    while (vcap.read(raw))
      writer[win].write(raw);
    writer[win].release();
    printf("\n");
  }

  // close temporary video then erase it
  vcap.release();
  remove(rname);
}


//= Disconnect from current video source (automatically called on exit).
// always gives most recently grabbed frame (not next one) hence some drops
// returns number of frames produced that were not consumed 

extern "C" DEXP int ocv_done ()
{
  abstime_t one_sec;
  int win;

  // stop background thread (if needed)
  tick.stop();
  if (run > 0)
  {
    run = 0;     
    pthread_timedjoin_np(hoover, 0, abstime_wait(&one_sec, 1000));
  }

  // close video reader and deallocate arrays 
  if (vcap.isOpened())
    vcap.release();          
  ocv_warp(0.0);             
  ok = -1;

  // finish off any video recordings
  for (win = 0; win < 6; win++)
    if (writer[win].isOpened())
      transcode(win);
  return((drop <= 1) ? 0 : drop - 1);  // stopped during last grab
}


///////////////////////////////////////////////////////////////////////////
//                           Display Functions                           //
///////////////////////////////////////////////////////////////////////////

//= Create a display window with given title and corner position.
// win is between 0 and 5 inclusive, titles must be unique
// if rec > 0 then saves images in "rec/<title>.mp4" ("rec" dir must exist)
// returns 1 if successful, 0 or negative for problem

extern "C" DEXP int ocv_win (int win, const char *title, int cx, int cy, int rec)
{
  // create default label if none given
  if ((win < 0) || (win > 5))
    return 0;
  if (title == NULL)
    sprintf_s(name[win], "Window %d", win);
  else
    strcpy_s(name[win], title);

  // remember desired top left corner position
  wx[win] = cx;
  wy[win] = cy;

  // intitialize mouse click info
  id[win] = win;             
  but[win] = 0;

  // set up to create a video containing each image (size unknown yet) 
  save[win] = rec;
  return 1;
}


//= Capture position of mouse click to global variables.

static void cb_mouse (int evt, int x, int y, int flag, void *param)
{
  int win = *((int *) param);        

  if ((evt == cv::EVENT_LBUTTONDOWN) || (evt == cv::EVENT_RBUTTONDOWN))
  {
    but[win] = ((evt == cv::EVENT_LBUTTONDOWN) ? 1 : 3);
    mx[win] = x;
    my[win] = y;
  }
}


//= Send an image to some window for display (must call ocv_show() later).
// buffer is left-to-right, bottom-up, BGR color order and size iw x ih
// if mark > 0 then saves as "rec/<title>_<mark>.bmp" ("rec" dir must exist)
// returns 1 if successful, 0 or negative for problem

extern "C" DEXP int ocv_queue (int win, const unsigned char *buf, int iw, int ih, int mark)
{
  cv::Mat bot, top;
  char fname[80];

  // sanity check
  if ((win < 0) || (win > 5))
    return -3;
  if (name[win][0] == '\0')
    return -2;
  if ((iw <= 0) || (ih <= 0))
    return -1;
  if (buf == NULL)
    return 0;

  // OpenCV images are top-down
  bot = cv::Mat(ih, iw, CV_8UC3, (void *) buf);
  cv::flip(bot, top, 0);
  cv::imshow(name[win], top);

  // see if window needs to be initialized
  if ((wx[win] >= 0) && (wy[win] >= 0))
  {
    cv::moveWindow(name[win], wx[win], wy[win]);
    cv::setMouseCallback(name[win], cb_mouse, (void *)(id + win));
    wx[win] = -1;
  }

  // possibly save still image (uncompressed)
  if (mark > 0)
  {
    sprintf_s(fname, "rec/%s_%04d.bmp", name[win], mark);
    cv::imwrite(fname, top);
  }

  // possibly add as frame to on-going video
  if ((save[win] > 0) && !writer[win].isOpened())
  {
    sprintf_s(fname, "rec/raw%d.avi", win);
    if (!writer[win].open(fname, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 30.0, cv::Size(iw, ih), true))
      save[win] = 0; 
  } 
  if (writer[win].isOpened())
    writer[win].write(top);
  return 1;
}


//= Update all display windows with queued buffers (blocks for 1 ms).

extern "C" DEXP void ocv_show ()
{
  cv::waitKey(1);
}


//= Checks a particular window for position of most recent mouse click.
// coords wrt to displayed buffer, y is TOP DOWN, clears status during call
// returns 0 if nothing, 1 for left button, 3 for right button

extern "C" DEXP int ocv_click (int win, int& x, int& y)
{
  int clk;

  // sanity check
  if ((win < 0) || (win > 5))
    return 0;

  // check if any click and reset status
  clk = but[win];
  if (clk <= 0)
    return 0;
  but[win] = 0;

  // pass on requested information
  x = mx[win];
  y = my[win];
  return clk;
}

