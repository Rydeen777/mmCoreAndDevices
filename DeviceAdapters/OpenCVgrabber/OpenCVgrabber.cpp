///////////////////////////////////////////////////////////////////////////////
// FILE:          TucsenCamera.cpp - based on DemoCamera.cpp
// PROJECT:       Micro-Manager
// SUBSYSTEM:     DeviceAdapters
//-----------------------------------------------------------------------------
// DESCRIPTION:   Implements capture from DirectShow and WDM class drivers.
//                Based heavily on the demo camera project.
//                
// AUTHOR:        Nenad Amodaj, nenad@amodaj.com, 06/08/2005
//			      Edited by Ed Simmons ed@esimaging.co.uk
// 
// COPYRIGHT:     University of California, San Francisco, 2006 
// LICENSE:       This file is distributed under the BSD license.
//                License text is included with the source distribution.
//
//                This file is distributed in the hope that it will be useful,
//                but WITHOUT ANY WARRANTY; without even the implied warranty
//                of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//
//                IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//                CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
//                INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES.
// CVS:           $Id: DemoCamera.cpp 7239 2011-05-17 19:52:35Z karlh $
//

#include "OpenCVgrabber.h"
#include <cstdio>
#include <string>
#include <math.h>
#include "../../MMDevice/ModuleInterface.h"
#include "../../MMCore/Error.h"
#include <sstream>
#include <algorithm>

#include <iostream>


// opencv includes
#include "..\..\..\3rdpartypublic\OpenCV2.1\include\opencv\cv.h"
#include "..\..\..\3rdpartypublic\OpenCV2.1\include\opencv\highgui.h"

using namespace cv;

using namespace std;


CvCapture* capture;
IplImage* frame; // do not modify, do not release!
IplImage* temp = 0; // used during conversion


const double COpenCVgrabber::nominalPixelSizeUm_ = 1.0;
double g_IntensityFactor_ = 1.0;

// External names used used by the rest of the system
// to load particular device from the "DemoCamera.dll" library
const char* g_CameraDeviceName = "OpenCVgrabber";

// constants for naming pixel types (allowed values of the "PixelType" property)
const char* g_PixelType_8bit = "8bit";
const char* g_PixelType_16bit = "16bit";
const char* g_PixelType_32bitRGB = "32bitRGB";
const char* g_PixelType_64bitRGB = "64bitRGB";
const char* g_PixelType_32bit = "32bit";  // floating point greyscale


// constants for naming resolution modes
const char* g_Keyword_Resolution = "Resolution";

const char* g_Res0 = "320x200";// cga
const char* g_Res1 = "320x240";//qvga 
const char* g_Res2 = "340x256"; 
const char* g_Res3 = "480x320"; 
const char* g_Res4 = "640x480";//vga 
const char* g_Res5 = "680x512"; 
const char* g_Res6 = "720x480";// 
const char* g_Res7 = "768x576";//pal 
const char* g_Res8 = "800x480";//wvga
const char* g_Res9 = "854x480";//wvga
const char* g_Res10 = "800x480";//svga
const char* g_Res11 = "1024x600";//wsvga
const char* g_Res12 = "1024x768";//xga
const char* g_Res13 = "1136x768";
const char* g_Res14 = "1280x720";//hd720
const char* g_Res15 = "1280x800";//wxga
const char* g_Res16 = "1280x960";
const char* g_Res17 = "1280x1024";//sxga
const char* g_Res18 = "1360x1024";
const char* g_Res19 = "1400x1050";//sxga+
const char* g_Res20 = "1440x900";
const char* g_Res21 = "1440x960";
const char* g_Res22 = "1600x1200";//uxga
const char* g_Res23 = "1680x1050";//wsxga+
const char* g_Res24 = "1920x1080";// are you ready for the hud?
const char* g_Res25 = "1920x1200";//wuxga
const char* g_Res26 = "2048x1080";//2k
const char* g_Res27 = "2048x1536";//qxga
const char* g_Res28 = "2560x1600";//wqxga
const char* g_Res29 = "2560x2048";//sqxga
const char* g_Res30 = "2592x1944";

const char* g_Keyword_Contrast = "Contrast";
const char* g_Keyword_Saturation = "Saturation";


// TODO: linux entry code

// windows DLL entry code
#ifdef WIN32
BOOL APIENTRY DllMain( HANDLE /*hModule*/, 
                      DWORD  ul_reason_for_call, 
                      LPVOID /*lpReserved*/
                      )
{
   switch (ul_reason_for_call)
   {
   case DLL_PROCESS_ATTACH:
   case DLL_THREAD_ATTACH:
   case DLL_THREAD_DETACH:
   case DLL_PROCESS_DETACH:
      break;
   }
   return TRUE;
}
#endif

///////////////////////////////////////////////////////////////////////////////
// Exported MMDevice API
///////////////////////////////////////////////////////////////////////////////

/**
 * List all suppoerted hardware devices here
 * Do not discover devices at runtime.  To avoid warnings about missing DLLs, Micro-Manager
 * maintains a list of supported device (MMDeviceList.txt).  This list is generated using 
 * information supplied by this function, so runtime discovery will create problems.
 */
MODULE_API void InitializeModuleData()
{
   AddAvailableDeviceName(g_CameraDeviceName, "OpenCVgrabber video input");
}

MODULE_API MM::Device* CreateDevice(const char* deviceName)
{
   if (deviceName == 0)
      return 0;

   // decide which device class to create based on the deviceName parameter
   if (strcmp(deviceName, g_CameraDeviceName) == 0)
   {
      // create camera
      return new COpenCVgrabber();
   }

   // ...supplied name not recognized
   return 0;
}

MODULE_API void DeleteDevice(MM::Device* pDevice)
{
   delete pDevice;
}

///////////////////////////////////////////////////////////////////////////////
// COpenCVgrabber implementation
// ~~~~~~~~~~~~~~~~~~~~~~~~~~

/**
* COpenCVgrabber constructor.
* Setup default all variables and create device properties required to exist
* before intialization. In this case, no such properties were required. All
* properties will be created in the Initialize() method.
*
* As a general guideline Micro-Manager devices do not access hardware in the
* the constructor. We should do as little as possible in the constructor and
* perform most of the initialization in the Initialize() method.
*/
COpenCVgrabber::COpenCVgrabber() :
   CCameraBase<COpenCVgrabber> (),
   dPhase_(0),
   initialized_(false),
   readoutUs_(0.0),
   scanMode_(1),
   bitDepth_(8),
   roiX_(0),
   roiY_(0),
   sequenceStartTime_(0),
	binSize_(1),
	cameraCCDXSize_(800),
	cameraCCDYSize_(600),
   nComponents_(4),
   pDemoResourceLock_(0),
   triggerDevice_(""),
	dropPixels_(false),
	saturatePixels_(false),
	fractionOfPixelsToDropOrSaturate_(0.002)
{
   memset(testProperty_,0,sizeof(testProperty_));

   // call the base class method to set-up default error codes/messages
   InitializeDefaultErrorMessages();
   readoutStartTime_ = GetCurrentMMTime();
   pDemoResourceLock_ = new MMThreadLock();
   thd_ = new MySequenceThread(this);
}

/**
* COpenCVgrabber destructor.
* If this device used as intended within the Micro-Manager system,
* Shutdown() will be always called before the destructor. But in any case
* we need to make sure that all resources are properly released even if
* Shutdown() was not called.
*/
COpenCVgrabber::~COpenCVgrabber()
{
	if(capture){
	   cvReleaseCapture(&capture);
	}
	if(temp){
	   cvReleaseImage(&temp);
	}
   StopSequenceAcquisition();
   delete thd_;
   delete pDemoResourceLock_;
}

/**
* Obtains device name.
* Required by the MM::Device API.
*/
void COpenCVgrabber::GetName(char* name) const
{
   // We just return the name we use for referring to this
   // device adapter.
   CDeviceUtils::CopyLimitedString(name, g_CameraDeviceName);
}

/**
* Intializes the hardware.
* Required by the MM::Device API.
* Typically we access and initialize hardware at this point.
* Device properties are typically created here as well, except
* the ones we need to use for defining initialization parameters.
* Such pre-initialization properties are created in the constructor.
* (This device does not have any pre-initialization properties)
*/
int COpenCVgrabber::Initialize()
{

   if (initialized_)
      return DEVICE_OK;

   // init the hardware

   // start opencv capture from first device, we need to initialise hardware early on to discover properties
   capture = cvCaptureFromCAM(CV_CAP_ANY);
   if (!capture) // do we have a capture device?
   {
     return DEVICE_NOT_CONNECTED;
   }
   frame = cvQueryFrame(capture);
   if (!frame)
   {
      printf("Cannot retrieve frame from camera!");
      return DEVICE_NOT_CONNECTED;
   }
   long w = cvGetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH);
   long h = cvGetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT);
   cameraCCDXSize_ = w;
   cameraCCDYSize_ = h;


   // set property list
   // -----------------

   // Name
   int nRet = CreateProperty(MM::g_Keyword_Name, g_CameraDeviceName, MM::String, true);
   if (DEVICE_OK != nRet)
      return nRet;

   // Description
   nRet = CreateProperty(MM::g_Keyword_Description, "OpenCVgrabber Device Adapter", MM::String, true);
   if (DEVICE_OK != nRet)
      return nRet;

   // CameraName
   nRet = CreateProperty(MM::g_Keyword_CameraName, "OpenCVgrabber video input", MM::String, true);
   assert(nRet == DEVICE_OK);

   // CameraID
   nRet = CreateProperty(MM::g_Keyword_CameraID, "V1.0", MM::String, true);
   assert(nRet == DEVICE_OK);

   // binning
   CPropertyAction *pAct = new CPropertyAction (this, &COpenCVgrabber::OnBinning);
   nRet = CreateProperty(MM::g_Keyword_Binning, "1", MM::Integer, false, pAct);
   assert(nRet == DEVICE_OK);

   nRet = SetAllowedBinning();
   if (nRet != DEVICE_OK)
      return nRet;

   // pixel type
   pAct = new CPropertyAction (this, &COpenCVgrabber::OnPixelType);
   nRet = CreateProperty(MM::g_Keyword_PixelType, g_PixelType_32bitRGB, MM::String, false, pAct);
   assert(nRet == DEVICE_OK);

   vector<string> pixelTypeValues;
   pixelTypeValues.push_back(g_PixelType_8bit);
   //pixelTypeValues.push_back(g_PixelType_16bit); 
	pixelTypeValues.push_back(g_PixelType_32bitRGB);
	//pixelTypeValues.push_back(g_PixelType_64bitRGB);
   //pixelTypeValues.push_back(::g_PixelType_32bit);

   nRet = SetAllowedValues(MM::g_Keyword_PixelType, pixelTypeValues);
   if (nRet != DEVICE_OK)
      return nRet;

   // Bit depth
   pAct = new CPropertyAction (this, &COpenCVgrabber::OnBitDepth);
   nRet = CreateProperty("BitDepth", "8", MM::Integer, false, pAct);
   assert(nRet == DEVICE_OK);

   vector<string> bitDepths;
   bitDepths.push_back("8");
   //bitDepths.push_back("10");
  //bitDepths.push_back("12");
   //bitDepths.push_back("14");
   //bitDepths.push_back("16");
   //bitDepths.push_back("32");
   nRet = SetAllowedValues("BitDepth", bitDepths);
   if (nRet != DEVICE_OK)
      return nRet;
   
   // Resolution


    pAct = new CPropertyAction (this, &COpenCVgrabber::OnResolution);
   nRet = CreateProperty(g_Keyword_Resolution, g_Res0, MM::String, false, pAct);
   assert(nRet == DEVICE_OK);

   vector<string> ResValues;

   ResValues.push_back(g_Res0);
   ResValues.push_back(g_Res1);
   ResValues.push_back(g_Res2);
   ResValues.push_back(g_Res3);
   ResValues.push_back(g_Res4);
   ResValues.push_back(g_Res5);
   ResValues.push_back(g_Res6);
   ResValues.push_back(g_Res7);
   ResValues.push_back(g_Res8);
   ResValues.push_back(g_Res9);
   ResValues.push_back(g_Res10);
   ResValues.push_back(g_Res11);
   ResValues.push_back(g_Res12);
   ResValues.push_back(g_Res13);
   ResValues.push_back(g_Res14);
   ResValues.push_back(g_Res15);
   ResValues.push_back(g_Res16);
   ResValues.push_back(g_Res17);
   ResValues.push_back(g_Res18);
   ResValues.push_back(g_Res19);
   ResValues.push_back(g_Res20);
   ResValues.push_back(g_Res21);
   ResValues.push_back(g_Res22);
   ResValues.push_back(g_Res23);
   ResValues.push_back(g_Res24);
   ResValues.push_back(g_Res25);
   ResValues.push_back(g_Res26);
   ResValues.push_back(g_Res27);
   ResValues.push_back(g_Res28);
   ResValues.push_back(g_Res29);
   ResValues.push_back(g_Res30);

   nRet = SetAllowedValues(g_Keyword_Resolution, ResValues);
   if (nRet != DEVICE_OK)
      return nRet;



   // exposure
   
   nRet = CreateProperty(MM::g_Keyword_Exposure,  "40"/*CDeviceUtils::ConvertToString(GetExposure())*/, MM::Float, false);
   assert(nRet == DEVICE_OK);
   SetPropertyLimits(MM::g_Keyword_Exposure, 0, 10000);

   /*
   // scan mode
   pAct = new CPropertyAction (this, &COpenCVgrabber::OnScanMode);
   nRet = CreateProperty("ScanMode", "1", MM::Integer, false, pAct);
   assert(nRet == DEVICE_OK);
   AddAllowedValue("ScanMode","1");
   //AddAllowedValue("ScanMode","2");
   //AddAllowedValue("ScanMode","3");
   */
   /*
   // camera gain
   pAct = new CPropertyAction (this, &COpenCVgrabber::OnGain);
   nRet = CreateProperty(MM::g_Keyword_Gain, "50", MM::Integer, false, pAct);
   assert(nRet == DEVICE_OK);
   SetPropertyLimits(MM::g_Keyword_Gain, 0, 1023);
   */
   // camera offset
   nRet = CreateProperty(MM::g_Keyword_Offset, "0", MM::Integer, false);
   assert(nRet == DEVICE_OK);
 
   // readout time
   pAct = new CPropertyAction (this, &COpenCVgrabber::OnReadoutTime);
   nRet = CreateProperty(MM::g_Keyword_ReadoutTime, "0", MM::Float, false, pAct);
   assert(nRet == DEVICE_OK);

   // CCD size of the camera we are using
   pAct = new CPropertyAction (this, &COpenCVgrabber::OnCameraCCDXSize);
   CreateProperty("OnCameraCCDXSize", /*"512"*/CDeviceUtils::ConvertToString(w), MM::Integer, false, pAct);
   pAct = new CPropertyAction (this, &COpenCVgrabber::OnCameraCCDYSize);
   CreateProperty("OnCameraCCDYSize", /*"512"*/CDeviceUtils::ConvertToString(h), MM::Integer, false, pAct);
 
   // synchronize all properties
   // --------------------------
   nRet = UpdateStatus();
   if (nRet != DEVICE_OK)
      return nRet;

   // setup the buffer
   // ----------------
   nRet = ResizeImageBuffer();
   if (nRet != DEVICE_OK)
      return nRet;

#ifdef TESTRESOURCELOCKING
   TestResourceLocking(true);
   LogMessage("TestResourceLocking OK",true);
#endif

   initialized_ = true;

   // initialize image buffer
   GenerateEmptyImage(img_);
   return DEVICE_OK;


}

/**
* Shuts down (unloads) the device.
* Required by the MM::Device API.
* Ideally this method will completely unload the device and release all resources.
* Shutdown() may be called multiple times in a row.
* After Shutdown() we should be allowed to call Initialize() again to load the device
* without causing problems.
*/
int COpenCVgrabber::Shutdown()
{
	if(capture){
	   cvReleaseCapture(&capture);
	}
	if(temp){
	   cvReleaseImage(&temp);
	}
   initialized_ = false;
   return DEVICE_OK;
}

/**
* Performs exposure and grabs a single image.
* This function should block during the actual exposure and return immediately afterwards 
* (i.e., before readout).  This behavior is needed for proper synchronization with the shutter.
* Required by the MM::Camera API.
*/
int COpenCVgrabber::SnapImage()
{
	static int callCounter = 0;
	++callCounter;

   MM::MMTime startTime = GetCurrentMMTime();
   double exp = GetExposure();
   double expUs = exp * 1000.0;
   //GenerateSyntheticImage(img_, exp);

   cvGrabFrame(capture);
   
   MM::MMTime s0(0,0);
   MM::MMTime t2 = GetCurrentMMTime();
   if( s0 < startTime )
   {
      // ensure wait time is non-negative
      long naptime = (long)(0.5 + expUs - (double)(t2-startTime).getUsec());
      if( naptime < 1)
         naptime = 1;
      // longest possible nap is about 38 minutes
      CDeviceUtils::NapMicros((unsigned long) naptime);
   }
   else
   {
      std::cerr << "You are operating this device adapter without setting the core callback, timing functions aren't yet available" << std::endl;
      // called without the core callback probably in off line test program
      // need way to build the core in the test program

   }
   readoutStartTime_ = GetCurrentMMTime();

   return DEVICE_OK;
}


/**
* Returns pixel data.
* Required by the MM::Camera API.
* The calling program will assume the size of the buffer based on the values
* obtained from GetImageBufferSize(), which in turn should be consistent with
* values returned by GetImageWidth(), GetImageHight() and GetImageBytesPerPixel().
* The calling program allso assumes that camera never changes the size of
* the pixel buffer on its own. In other words, the buffer can change only if
* appropriate properties are set (such as binning, pixel type, etc.)
*/
const unsigned char* COpenCVgrabber::GetImageBuffer()
{

   MMThreadGuard g(imgPixelsLock_);
   MM::MMTime readoutTime(readoutUs_);
   while (readoutTime > (GetCurrentMMTime() - readoutStartTime_)) {}

   temp = cvRetrieveFrame(capture);
   if(!temp) return 0;

   char buf[MM::MaxStrLength];
   GetProperty(MM::g_Keyword_PixelType, buf);
   std::string pixelType(buf);

   if (pixelType.compare(g_PixelType_32bitRGB) == 0)
   {
	   if(roiX_ == 0 && roiY_ == 0){
		   for(int i=0; i < temp->width * temp->height; i++){
				memcpy(img_.GetPixelsRW()+i*4, temp->imageData+i*3,3);
			} 
	   } else {
			cvSetImageROI(temp,cvRect(roiX_,roiY_,img_.Width(),img_.Height()));
			IplImage *ROI = cvCreateImage(cvSize(img_.Width(),img_.Height()),
							   temp->depth,
                               temp->nChannels);

			
			if(!ROI) return 0; //failed to create ROI image
			cvCopy(temp,ROI,NULL);
			cvResetImageROI(temp);
			
			for(int i=0; i < ROI->width * ROI->height; i++){
				memcpy(img_.GetPixelsRW()+i*4, ROI->imageData+i*3,3);
			}
			cvReleaseImage(&ROI);
	   }
		
   } else {
	   // TODO: return a proper greyscale image rather than the first channel of a colour one!!
		for(int i=0; i < cameraCCDXSize_ * cameraCCDYSize_; i++){
			memcpy(img_.GetPixelsRW()+i, temp->imageData+i*3,1);
		}
   }


   return (unsigned char*)(img_.GetPixels());
}

/**
* Returns image buffer X-size in pixels.
* Required by the MM::Camera API.
*/
unsigned COpenCVgrabber::GetImageWidth() const
{
   
   return img_.Width();
}

/**
* Returns image buffer Y-size in pixels.
* Required by the MM::Camera API.
*/
unsigned COpenCVgrabber::GetImageHeight() const
{
   return img_.Height();
}

/**
* Returns image buffer pixel depth in bytes.
* Required by the MM::Camera API.
*/
unsigned COpenCVgrabber::GetImageBytesPerPixel() const
{
   return img_.Depth();
} 

/**
* Returns the bit depth (dynamic range) of the pixel.
* This does not affect the buffer size, it just gives the client application
* a guideline on how to interpret pixel values.
* Required by the MM::Camera API.
*/
unsigned COpenCVgrabber::GetBitDepth() const
{
   return bitDepth_;
}

/**
* Returns the size in bytes of the image buffer.
* Required by the MM::Camera API.
*/
long COpenCVgrabber::GetImageBufferSize() const
{
   return img_.Width() * img_.Height() * GetImageBytesPerPixel();
}

/**
* Sets the camera Region Of Interest.
* Required by the MM::Camera API.
* This command will change the dimensions of the image.
* Depending on the hardware capabilities the camera may not be able to configure the
* exact dimensions requested - but should try do as close as possible.
* If the hardware does not have this capability the software should simulate the ROI by
* appropriately cropping each frame.
* This demo implementation ignores the position coordinates and just crops the buffer.
* @param x - top-left corner coordinate
* @param y - top-left corner coordinate
* @param xSize - width
* @param ySize - height
*/
int COpenCVgrabber::SetROI(unsigned x, unsigned y, unsigned xSize, unsigned ySize)
{

   if (xSize == 0 && ySize == 0)
   {
      // effectively clear ROI
      ResizeImageBuffer();
      roiX_ = 0;
      roiY_ = 0;
   }
   else
   {
      // apply ROI
      img_.Resize(xSize, ySize);
      roiX_ = x;
      roiY_ = y;
   }
   return DEVICE_OK;
}

/**
* Returns the actual dimensions of the current ROI.
* Required by the MM::Camera API.
*/
int COpenCVgrabber::GetROI(unsigned& x, unsigned& y, unsigned& xSize, unsigned& ySize)
{


   x = roiX_;
   y = roiY_;

   xSize = img_.Width();
   ySize = img_.Height();

   return DEVICE_OK;
}

/**
* Resets the Region of Interest to full frame.
* Required by the MM::Camera API.
*/
int COpenCVgrabber::ClearROI()
{
   ResizeImageBuffer();
   roiX_ = 0;
   roiY_ = 0;
      
   return DEVICE_OK;
}

/**
* Returns the current exposure setting in milliseconds.
* Required by the MM::Camera API.
*/
double COpenCVgrabber::GetExposure() const
{
	//double exp = cvGetCaptureProperty(capture,CV_CAP_PROP_EXPOSURE); // try to get the exposure from OpenCV - not all drivers allow this
	//if(exp != 0) return exp; // if it works, great, return it, otherwise...


   char buf[MM::MaxStrLength];
   int ret = GetProperty(MM::g_Keyword_Exposure, buf);
   if (ret != DEVICE_OK)
      return 0.0;
   return atof(buf); // just return the exposure MM thinks it is using
   
}

/**
* Sets exposure in milliseconds.
* Required by the MM::Camera API.
*/
void COpenCVgrabber::SetExposure(double exp)
{
   SetProperty(MM::g_Keyword_Exposure, CDeviceUtils::ConvertToString(exp));
   cvSetCaptureProperty(capture,CV_CAP_PROP_EXPOSURE,exp); // there is no benefit from checking if this works (many capture drivers via opencv just don't allow this) - just carry on regardless.
}

/**
* Returns the current binning factor.
* Required by the MM::Camera API.
*/
int COpenCVgrabber::GetBinning() const
{

   char buf[MM::MaxStrLength];
   int ret = GetProperty(MM::g_Keyword_Binning, buf);
   if (ret != DEVICE_OK)
      return 1;
   return atoi(buf);
}

/**
* Sets binning factor.
* Required by the MM::Camera API.
*/
int COpenCVgrabber::SetBinning(int binF)
{
   return SetProperty(MM::g_Keyword_Binning, CDeviceUtils::ConvertToString(binF));
}

int COpenCVgrabber::SetAllowedBinning() 
{
   vector<string> binValues;
   binValues.push_back("1");
   /*
   binValues.push_back("2");
   if (scanMode_ < 3)
      binValues.push_back("4");
   if (scanMode_ < 2)
      binValues.push_back("8");
   if (binSize_ == 8 && scanMode_ == 3) {
      SetProperty(MM::g_Keyword_Binning, "2");
   } else if (binSize_ == 8 && scanMode_ == 2) {
      SetProperty(MM::g_Keyword_Binning, "4");
   } else if (binSize_ == 4 && scanMode_ == 3) {
      SetProperty(MM::g_Keyword_Binning, "2");
   }
      */
   LogMessage("Setting Allowed Binning settings", true);
   return SetAllowedValues(MM::g_Keyword_Binning, binValues);
}


/**
 * Required by the MM::Camera API
 * Please implement this yourself and do not rely on the base class implementation
 * The Base class implementation is deprecated and will be removed shortly
 */
int COpenCVgrabber::StartSequenceAcquisition(double interval) {
   return StartSequenceAcquisition(LONG_MAX, interval, false);            
}

/**                                                                       
* Stop and wait for the Sequence thread finished                                   
*/                                                                        
int COpenCVgrabber::StopSequenceAcquisition()                                     
{                                                                         

   if (!thd_->IsStopped()) {
      thd_->Stop();                                                       
      thd_->wait();                                                       
   }                                                                      
                                                                          
   return DEVICE_OK;                                                      
} 

/**
* Simple implementation of Sequence Acquisition
* A sequence acquisition should run on its own thread and transport new images
* coming of the camera into the MMCore circular buffer.
*/
int COpenCVgrabber::StartSequenceAcquisition(long numImages, double interval_ms, bool stopOnOverflow)
{

   //if (IsCapturing())
   //   return DEVICE_CAMERA_BUSY_ACQUIRING;

   int ret = GetCoreCallback()->PrepareForAcq(this);
   if (ret != DEVICE_OK)
      return ret;
   sequenceStartTime_ = GetCurrentMMTime();
   imageCounter_ = 0;
   thd_->Start(numImages,interval_ms);
   stopOnOverflow_ = stopOnOverflow;
   return DEVICE_OK;
}

/*
 * Inserts Image and MetaData into MMCore circular Buffer
 */
int COpenCVgrabber::InsertImage()
{
   MM::MMTime timeStamp = this->GetCurrentMMTime();
   char label[MM::MaxStrLength];
   this->GetLabel(label);
 
   // Important:  metadata about the image are generated here:
   Metadata md;
   md.put("Camera", label);
   md.put(MM::g_Keyword_Metadata_StartTime, CDeviceUtils::ConvertToString(sequenceStartTime_.getMsec()));
   md.put(MM::g_Keyword_Elapsed_Time_ms, CDeviceUtils::ConvertToString((timeStamp - sequenceStartTime_).getMsec()));
   md.put(MM::g_Keyword_Metadata_ImageNumber, CDeviceUtils::ConvertToString(imageCounter_));
   md.put(MM::g_Keyword_Metadata_ROI_X, CDeviceUtils::ConvertToString( (long) roiX_)); 
   md.put(MM::g_Keyword_Metadata_ROI_Y, CDeviceUtils::ConvertToString( (long) roiY_)); 

   imageCounter_++;

   char buf[MM::MaxStrLength];
   GetProperty(MM::g_Keyword_Binning, buf);
   md.put(MM::g_Keyword_Binning, buf);

   MMThreadGuard g(imgPixelsLock_);


   const unsigned char* pI = GetImageBuffer();
   unsigned int w = GetImageWidth();
   unsigned int h = GetImageHeight();
   unsigned int b = GetImageBytesPerPixel();

   int ret = GetCoreCallback()->InsertImage(this, pI, w, h, b, &md);
   if (!stopOnOverflow_ && ret == DEVICE_BUFFER_OVERFLOW)
   {
      // do not stop on overflow - just reset the buffer
      GetCoreCallback()->ClearImageBuffer(this);
      // don't process this same image again...
      return GetCoreCallback()->InsertImage(this, pI, w, h, b, &md, false);
   } else
      return ret;
}

/*
 * Do actual capturing
 * Called from inside the thread  
 */
int COpenCVgrabber::ThreadRun (void)
{
   int ret=DEVICE_ERR;
   
   // Trigger
   if (triggerDevice_.length() > 0) {
      MM::Device* triggerDev = GetDevice(triggerDevice_.c_str());
      if (triggerDev != 0) {
      	//char label[256];
      	//triggerDev->GetLabel(label);
      	LogMessage("trigger requested");
      	triggerDev->SetProperty("Trigger","+");
      }
   }
   ret = SnapImage();
   if(ret != DEVICE_OK) return ret;
   
   /*
   temp = cvQueryFrame(capture);
   if(!temp) return DEVICE_ERR;
   for(int i=0; i < temp->width * temp->height; i++){
				memcpy(img_.GetPixelsRW()+i*4, temp->imageData+i*3,3);
			}
*/
   ret = InsertImage();
   if (ret != DEVICE_OK)
   {
      return ret;
   }
   
   return ret;
};

bool COpenCVgrabber::IsCapturing() {
   return !thd_->IsStopped();
}

/*
 * called from the thread function before exit 
 */
void COpenCVgrabber::OnThreadExiting() throw()
{
   try
   {
	  
      LogMessage(g_Msg_SEQUENCE_ACQUISITION_THREAD_EXITING);
      GetCoreCallback()?GetCoreCallback()->AcqFinished(this,0):DEVICE_OK;
   }

   catch( CMMError& e){
      std::ostringstream oss;
      oss << g_Msg_EXCEPTION_IN_ON_THREAD_EXITING << " " << e.getMsg() << " " << e.getCode();
      LogMessage(oss.str().c_str(), false);
   }
   catch(...)
   {
      LogMessage(g_Msg_EXCEPTION_IN_ON_THREAD_EXITING, false);
   }
}


MySequenceThread::MySequenceThread(COpenCVgrabber* pCam)
   :intervalMs_(default_intervalMS)
   ,numImages_(default_numImages)
   ,imageCounter_(0)
   ,stop_(true)
   ,suspend_(false)
   ,camera_(pCam)
   ,startTime_(0)
   ,actualDuration_(0)
   ,lastFrameTime_(0)
{};

MySequenceThread::~MySequenceThread() {};

void MySequenceThread::Stop() {
   MMThreadGuard(this->stopLock_);
   stop_=true;
}

void MySequenceThread::Start(long numImages, double intervalMs)
{
   MMThreadGuard(this->stopLock_);
   MMThreadGuard(this->suspendLock_);
   numImages_=numImages;
   intervalMs_=intervalMs;
   imageCounter_=0;
   stop_ = false;
   suspend_=false;
   activate();
   actualDuration_ = 0;
   startTime_= camera_->GetCurrentMMTime();
   lastFrameTime_ = 0;
}

bool MySequenceThread::IsStopped(){
   MMThreadGuard(this->stopLock_);
   return stop_;
}

void MySequenceThread::Suspend() {
   MMThreadGuard(this->suspendLock_);
   suspend_ = true;
}

bool MySequenceThread::IsSuspended() {
   MMThreadGuard(this->suspendLock_);
   return suspend_;
}

void MySequenceThread::Resume() {
   MMThreadGuard(this->suspendLock_);
   suspend_ = false;
}

int MySequenceThread::svc(void) throw()
{
   int ret=DEVICE_ERR;
   try 
   {
      do
      {  
         ret=camera_->ThreadRun();
      } while (DEVICE_OK == ret && !IsStopped() && !IsSuspended()  && imageCounter_++ < numImages_-1);
      if(IsSuspended())
		  camera_->LogMessage("SeqAcquisition suspended by the user\n");
	  if (IsStopped())
         camera_->LogMessage("SeqAcquisition interrupted by the user\n");

   }catch( CMMError& e){
      camera_->LogMessage(e.getMsg(), false);
      ret = e.getCode();
   }catch(...){
      camera_->LogMessage(g_Msg_EXCEPTION_IN_THREAD, false);
   }
   stop_=true;
   actualDuration_ = camera_->GetCurrentMMTime() - startTime_;
   camera_->OnThreadExiting();
   return ret;
}


///////////////////////////////////////////////////////////////////////////////
// COpenCVgrabber Action handlers
///////////////////////////////////////////////////////////////////////////////

/*
* this Read Only property will update whenever any property is modified
*/

int COpenCVgrabber::OnTestProperty(MM::PropertyBase* pProp, MM::ActionType eAct, long indexx)
{

   if (eAct == MM::BeforeGet)
   {
      pProp->Set(testProperty_[indexx]);
   }
   else if (eAct == MM::AfterSet)
   {
      pProp->Get(testProperty_[indexx]);
   }
	return DEVICE_OK;

}

// handles contrast property
/*
int COpenCVgrabber::OnContrast(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   int ret = DEVICE_ERR;
   switch(eAct)
   {
   case MM::AfterSet:
      {
         long contrast;
         pProp->Get(contrast);
		 ret = TS1000IICameraSetContrast(contrast);
		 if(ret!=STATUS_OK) return DEVICE_ERR;
		 ret=DEVICE_OK;
		 			
      }break;
   case MM::BeforeGet:
      {
		  byte contrast;
		  ret = TS1000IICameraGetContrast(&contrast);
		  if(ret!=STATUS_OK) return DEVICE_ERR;
          ret=DEVICE_OK;
			pProp->Set((double)contrast);
      }break;
   }
   return ret; 
}
*/


// handles gain property

int COpenCVgrabber::OnGain(MM::PropertyBase* pProp, MM::ActionType eAct)
{

   int ret = DEVICE_ERR;
   switch(eAct)
   {
   case MM::AfterSet:
      {

         long gain;
         pProp->Get(gain);
		 cvSetCaptureProperty(capture,CV_CAP_PROP_GAIN,gain);
		 ret=DEVICE_OK;
      }break;
   case MM::BeforeGet:
      {
         
		 double gain;
		 gain = cvGetCaptureProperty(capture,CV_CAP_PROP_GAIN);
		 if(!gain) return DEVICE_ERR;
		 ret=DEVICE_OK;
			pProp->Set((double)gain);
      }break;
   }
   return ret; 
}

// handles saturation property
/*
int COpenCVgrabber::OnSaturation(MM::PropertyBase* pProp, MM::ActionType eAct)
{

   int ret = DEVICE_ERR;
   switch(eAct)
   {
   case MM::AfterSet:
      {
         long sat;
         pProp->Get(sat);
		 ret = TS1000IICameraSetColorEnhancement(TRUE);
		 if(ret!= STATUS_OK) return DEVICE_ERR;
		 ret = TS1000IICameraSetSaturation(sat);
		 if(ret!= STATUS_OK) return DEVICE_ERR;
		 ret=DEVICE_OK;
      }break;
   case MM::BeforeGet:
      {
         ret=DEVICE_OK;
		 BYTE sat;
		 ret = TS1000IICameraGetSaturation(&sat);
		 if(ret!= STATUS_OK) return DEVICE_ERR;
		 ret=DEVICE_OK;
			pProp->Set((double)sat);
      }break;
   }
   return ret; 
}
*/
/**
* Handles "Binning" property.
*/
int COpenCVgrabber::OnBinning(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   int ret = DEVICE_ERR;
   switch(eAct)
   {
   case MM::AfterSet:
      {
      //   if(IsCapturing())
      //      return DEVICE_CAMERA_BUSY_ACQUIRING;

         // the user just set the new value for the property, so we have to
         // apply this value to the 'hardware'.
         long binFactor;
         pProp->Get(binFactor);
			if(binFactor > 0 && binFactor < 10)
			{
				img_.Resize(cameraCCDXSize_/binFactor, cameraCCDYSize_/binFactor);
				binSize_ = binFactor;
            std::ostringstream os;
            os << binSize_;
            OnPropertyChanged("Binning", os.str().c_str());
				ret=DEVICE_OK;
			}
      }break;
   case MM::BeforeGet:
      {
         ret=DEVICE_OK;
			pProp->Set(binSize_);
      }break;
   }
   return ret; 
}

/**
* Handles "PixelType" property.
*/
int COpenCVgrabber::OnPixelType(MM::PropertyBase* pProp, MM::ActionType eAct)
{

   int ret = DEVICE_ERR;
   switch(eAct)
   {
   case MM::AfterSet:
      {
         if(IsCapturing())
            return DEVICE_CAMERA_BUSY_ACQUIRING;

         string pixelType;
         pProp->Get(pixelType);

         if (pixelType.compare(g_PixelType_8bit) == 0)
         {
            nComponents_ = 1;
            img_.Resize(img_.Width(), img_.Height(), 1);
            bitDepth_ = 8;

            ret=DEVICE_OK;
         }
         else if (pixelType.compare(g_PixelType_16bit) == 0)
         {
            nComponents_ = 1;
            img_.Resize(img_.Width(), img_.Height(), 2);
            ret=DEVICE_OK;
         }
			else if ( pixelType.compare(g_PixelType_32bitRGB) == 0)
			{
            nComponents_ = 4;
            img_.Resize(img_.Width(), img_.Height(), 4);

            ret=DEVICE_OK;
			}
			else if ( pixelType.compare(g_PixelType_64bitRGB) == 0)
			{
            nComponents_ = 4;
            img_.Resize(img_.Width(), img_.Height(), 8);
            ret=DEVICE_OK;
			}
         else if ( pixelType.compare(g_PixelType_32bit) == 0)
			{
            nComponents_ = 1;
            img_.Resize(img_.Width(), img_.Height(), 4);
            ret=DEVICE_OK;
			}
         else
         {
            // on error switch to default pixel type
            nComponents_ = 1;
            img_.Resize(img_.Width(), img_.Height(), 1);
            pProp->Set(g_PixelType_8bit);
            ret = ERR_UNKNOWN_MODE;
         }
      } break;
   case MM::BeforeGet:
      {
		  /*
         long bytesPerPixel = GetImageBytesPerPixel();
         if (bytesPerPixel == 1)
         	pProp->Set(g_PixelType_8bit);
         else if (bytesPerPixel == 2)
         	pProp->Set(g_PixelType_16bit);
         else if (bytesPerPixel == 4)
         {
            if(4 == this->nComponents_) // todo SEPARATE bitdepth from #components
				   pProp->Set(g_PixelType_32bitRGB);
            else if( 1 == nComponents_)
               pProp->Set(::g_PixelType_32bit);
         }
         else if (bytesPerPixel == 8) // todo SEPARATE bitdepth from #components
				pProp->Set(g_PixelType_64bitRGB);
			else
				pProp->Set(g_PixelType_8bit);
				*/
         ret=DEVICE_OK;
		 
      }break;
   }
   return ret; 
}

/**
* Handles "BitDepth" property.
*/
int COpenCVgrabber::OnBitDepth(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   int ret = DEVICE_ERR;
   switch(eAct)
   {
   case MM::AfterSet:
      {
         if(IsCapturing())
            return DEVICE_CAMERA_BUSY_ACQUIRING;

         long bitDepth;
         pProp->Get(bitDepth);

			unsigned int bytesPerComponent;

         switch (bitDepth) {
            case 8:
					bytesPerComponent = 1;
               bitDepth_ = 8;
               ret=DEVICE_OK;
            break;
            case 10:
					bytesPerComponent = 2;
               bitDepth_ = 10;
               ret=DEVICE_OK;
            break;
            case 12:
					bytesPerComponent = 2;
               bitDepth_ = 12;
               ret=DEVICE_OK;
            break;
            case 14:
					bytesPerComponent = 2;
               bitDepth_ = 14;
               ret=DEVICE_OK;
            break;
            case 16:
					bytesPerComponent = 2;
               bitDepth_ = 16;
               ret=DEVICE_OK;
            break;
            case 32:
               bytesPerComponent = 4;
               bitDepth_ = 32; 
               ret=DEVICE_OK;
            break;
            default: 
               // on error switch to default pixel type
					bytesPerComponent = 1;

               pProp->Set((long)8);
               bitDepth_ = 8;
               ret = ERR_UNKNOWN_MODE;
            break;
         }
			char buf[MM::MaxStrLength];
			GetProperty(MM::g_Keyword_PixelType, buf);
			std::string pixelType(buf);
			unsigned int bytesPerPixel = 1;
			

         // automagickally change pixel type when bit depth exceeds possible value
         if (pixelType.compare(g_PixelType_8bit) == 0)
         {
				if( 2 == bytesPerComponent)
				{
					SetProperty(MM::g_Keyword_PixelType, g_PixelType_16bit);
					bytesPerPixel = 2;
				}
				else if ( 4 == bytesPerComponent)
            {
					SetProperty(MM::g_Keyword_PixelType, g_PixelType_32bit);
					bytesPerPixel = 4;

            }else
				{
				   bytesPerPixel = 1;
				}
         }
         else if (pixelType.compare(g_PixelType_16bit) == 0)
         {
				bytesPerPixel = 2;
         }
			else if ( pixelType.compare(g_PixelType_32bitRGB) == 0)
			{
				bytesPerPixel = 4;
			}
			else if ( pixelType.compare(g_PixelType_32bit) == 0)
			{
				bytesPerPixel = 4;
			}
			else if ( pixelType.compare(g_PixelType_64bitRGB) == 0)
			{
				bytesPerPixel = 8;
			}
			img_.Resize(img_.Width(), img_.Height(), bytesPerPixel);

      } break;
   case MM::BeforeGet:
      {
         pProp->Set((long)bitDepth_);
         ret=DEVICE_OK;
      }break;
   }
   return ret; 
}
/**
* Handles "Resolution" property.
*/
int COpenCVgrabber::OnResolution(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   int ret = DEVICE_ERR;
   switch(eAct)
   {
   case MM::AfterSet:
      {
         if(IsCapturing())
            return DEVICE_CAMERA_BUSY_ACQUIRING;

		 std::string resolution;
         pProp->Get(resolution);

		 if (resolution.compare(g_Res0) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 320);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 200);
			pProp->Set(g_Res0);
         } else if (resolution.compare(g_Res1) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 320);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 240);
			pProp->Set(g_Res1);
         } else if (resolution.compare(g_Res2) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 340);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 256);
			pProp->Set(g_Res2);
         } else if (resolution.compare(g_Res3) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 480);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 320);
			pProp->Set(g_Res3);
         } else if (resolution.compare(g_Res4) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 640);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 480);
			pProp->Set(g_Res4);
         } else if (resolution.compare(g_Res5) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 680);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 512);
			pProp->Set(g_Res5);
         } else if (resolution.compare(g_Res6) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 720);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 480);
			pProp->Set(g_Res6);
         } else if (resolution.compare(g_Res7) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 768);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 576);
			pProp->Set(g_Res7);
         } else if (resolution.compare(g_Res8) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 800);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 480);
			pProp->Set(g_Res8);
         } else if (resolution.compare(g_Res9) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 854);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 480);
			pProp->Set(g_Res9);
         } else if (resolution.compare(g_Res10) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 800);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 600);
			pProp->Set(g_Res10);
         } else if (resolution.compare(g_Res11) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 1024);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 600);
			pProp->Set(g_Res11);
         } else if (resolution.compare(g_Res12) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 1024);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 768);
			pProp->Set(g_Res12);
         } else if (resolution.compare(g_Res13) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 1136);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 768);
			pProp->Set(g_Res13);
         } else if (resolution.compare(g_Res14) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 1280);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 720);
			pProp->Set(g_Res14);
         } else if (resolution.compare(g_Res15) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 1280);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 800);
			pProp->Set(g_Res15);
         } else if (resolution.compare(g_Res16) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 1280);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 960);
			pProp->Set(g_Res16);
         } else if (resolution.compare(g_Res17) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 1280);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 1024);
			pProp->Set(g_Res17);
         } else if (resolution.compare(g_Res18) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 1360);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 1024);
			pProp->Set(g_Res18);
         } else if (resolution.compare(g_Res19) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 1400);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 1050);
			pProp->Set(g_Res19);
         } else if (resolution.compare(g_Res20) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 1440);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 900);
			pProp->Set(g_Res20);
         } else if (resolution.compare(g_Res21) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 1440);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 960);
			pProp->Set(g_Res21);
         } else if (resolution.compare(g_Res22) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 1600);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 1200);
			pProp->Set(g_Res22);
         } else if (resolution.compare(g_Res23) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 1680);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 1050);
			pProp->Set(g_Res23);
         } else if (resolution.compare(g_Res24) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 1920);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 1080);
			pProp->Set(g_Res24);
         } else if (resolution.compare(g_Res25) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 1920);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 1200);
			pProp->Set(g_Res25);
         } else if (resolution.compare(g_Res26) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 2048);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 1080);
			pProp->Set(g_Res26);
         } else if (resolution.compare(g_Res27) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 2048);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 1536);
			pProp->Set(g_Res27);
         } else if (resolution.compare(g_Res28) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 2560);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 1600);
			pProp->Set(g_Res28);
         } else if (resolution.compare(g_Res29) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 2560);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 2048);
			pProp->Set(g_Res29);
         } else if (resolution.compare(g_Res30) == 0)
         {
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH, 2592);
			cvSetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT, 1944);
			pProp->Set(g_Res30);
         } else  {
			 ret = DEVICE_ERR;
			 pProp->Set("Error");
		 }
		 if(ret != DEVICE_OK){
			 return ret;
		 }

		 cameraCCDXSize_ = cvGetCaptureProperty(capture, CV_CAP_PROP_FRAME_WIDTH);
		 cameraCCDYSize_ = cvGetCaptureProperty(capture, CV_CAP_PROP_FRAME_HEIGHT);

		 ret = ResizeImageBuffer();
		 if (ret != DEVICE_OK) return ret;

      } break;
   case MM::BeforeGet:
      {
         //pProp->Set((long)bitDepth_);
         ret=DEVICE_OK;
      }break;
   }
   return ret; 
}

/**
* Handles "ReadoutTime" property.
*/
int COpenCVgrabber::OnReadoutTime(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   if (eAct == MM::AfterSet)
   {
      double readoutMs;
      pProp->Get(readoutMs);

      readoutUs_ = readoutMs * 1000.0;
   }
   else if (eAct == MM::BeforeGet)
   {
      pProp->Set(readoutUs_ / 1000.0);
   }

   return DEVICE_OK;
}



/*
* Handles "ScanMode" property.
* Changes allowed Binning values to test whether the UI updates properly
*/
int COpenCVgrabber::OnScanMode(MM::PropertyBase* pProp, MM::ActionType eAct)
{ 

   if (eAct == MM::AfterSet) {
      pProp->Get(scanMode_);
      SetAllowedBinning();
      if (initialized_) {
         int ret = OnPropertiesChanged();
         if (ret != DEVICE_OK)
            return ret;
      }
   } else if (eAct == MM::BeforeGet) {
      LogMessage("Reading property ScanMode", true);
      pProp->Set(scanMode_);
   }
   return DEVICE_OK;
}




int COpenCVgrabber::OnCameraCCDXSize(MM::PropertyBase* pProp , MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet)
   {
		pProp->Set(cameraCCDXSize_);
   }
   else if (eAct == MM::AfterSet)
   {
      long value;
      pProp->Get(value);
		if ( (value < 16) || (33000 < value))
			return DEVICE_ERR;  // invalid image size
		if( value != cameraCCDXSize_)
		{
			cameraCCDXSize_ = value;
			img_.Resize(cameraCCDXSize_/binSize_, cameraCCDYSize_/binSize_);
		}
   }
	return DEVICE_OK;

}

int COpenCVgrabber::OnCameraCCDYSize(MM::PropertyBase* pProp, MM::ActionType eAct)
{

   if (eAct == MM::BeforeGet)
   {
		pProp->Set(cameraCCDYSize_);
   }
   else if (eAct == MM::AfterSet)
   {
      long value;
      pProp->Get(value);
		if ( (value < 16) || (33000 < value))
			return DEVICE_ERR;  // invalid image size
		if( value != cameraCCDYSize_)
		{
			cameraCCDYSize_ = value;
			img_.Resize(cameraCCDXSize_/binSize_, cameraCCDYSize_/binSize_);
		}
   }
	return DEVICE_OK;

}

int COpenCVgrabber::OnTriggerDevice(MM::PropertyBase* pProp, MM::ActionType eAct)
{

   if (eAct == MM::BeforeGet)
   {
      pProp->Set(triggerDevice_.c_str());
   }
   else if (eAct == MM::AfterSet)
   {
      pProp->Get(triggerDevice_);
   }
   return DEVICE_OK;
}


///////////////////////////////////////////////////////////////////////////////
// Private COpenCVgrabber methods
///////////////////////////////////////////////////////////////////////////////

/**
* Sync internal image buffer size to the chosen property values.
*/
int COpenCVgrabber::ResizeImageBuffer()
{

   char buf[MM::MaxStrLength];
   //int ret = GetProperty(MM::g_Keyword_Binning, buf);
   //if (ret != DEVICE_OK)
   //   return ret;
   //binSize_ = atol(buf);

   int ret = GetProperty(MM::g_Keyword_PixelType, buf);
   if (ret != DEVICE_OK)
      return ret;

	std::string pixelType(buf);
	int byteDepth = 0;

   if (pixelType.compare(g_PixelType_8bit) == 0)
   {
      byteDepth = 1;
   }
   else if (pixelType.compare(g_PixelType_16bit) == 0)
   {
      byteDepth = 2;
   }
	else if ( pixelType.compare(g_PixelType_32bitRGB) == 0)
	{
      byteDepth = 4;
	}
	else if ( pixelType.compare(g_PixelType_32bit) == 0)
	{
      byteDepth = 4;
	}
	else if ( pixelType.compare(g_PixelType_64bitRGB) == 0)
	{
      byteDepth = 8;
	}

   img_.Resize(cameraCCDXSize_/binSize_, cameraCCDYSize_/binSize_, byteDepth);
   return DEVICE_OK;
}

void COpenCVgrabber::GenerateEmptyImage(ImgBuffer& img)
{
   MMThreadGuard g(imgPixelsLock_);
   if (img.Height() == 0 || img.Width() == 0 || img.Depth() == 0)
      return;
   unsigned char* pBuf = const_cast<unsigned char*>(img.GetPixels());
   memset(pBuf, 0, img.Height()*img.Width()*img.Depth());
}



void COpenCVgrabber::TestResourceLocking(const bool recurse)
{
   MMThreadGuard g(*pDemoResourceLock_);
   if(recurse)
      TestResourceLocking(false);
}
