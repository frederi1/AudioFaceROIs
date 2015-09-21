//------------------------------------------------------------------------------
// <copyright file="FaceBasics.h" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------

#pragma once

#include "resource.h"
#include "ImageRenderer.h"

class CFaceBasics
{
    static const int       cColorWidth  = 1920;
    static const int       cColorHeight = 1080;

public:
    /// <summary>
    /// Constructor
    /// </summary>
    CFaceBasics();

    /// <summary>
    /// Destructor
    /// </summary>
    ~CFaceBasics();

    /// <summary>
    /// Handles window messages, passes most to the class instance to handle
    /// </summary>
    /// <param name="hWnd">window message is for</param>
    /// <param name="uMsg">message</param>
    /// <param name="wParam">message data</param>
    /// <param name="lParam">additional message data</param>
    /// <returns>result of message processing</returns>
    static LRESULT CALLBACK  MessageRouter(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    /// <summary>
    /// Handle windows messages for a class instance
    /// </summary>
    /// <param name="hWnd">window message is for</param>
    /// <param name="uMsg">message</param>
    /// <param name="wParam">message data</param>
    /// <param name="lParam">additional message data</param>
    /// <returns>result of message processing</returns>
    LRESULT CALLBACK       DlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    /// <summary>
    /// Creates the main window and begins processing
    /// </summary>
    /// <param name="hInstance"></param>
    /// <param name="nCmdShow"></param>
    int                    Run(HINSTANCE hInstance, int nCmdShow);

private:
    /// <summary>
    /// Main processing function
    /// </summary>
    void                   Update();

    /// <summary>
    /// Initializes the default Kinect sensor
    /// </summary>
    /// <returns>S_OK on success else the failure code</returns>
    HRESULT                InitializeDefaultSensor();

    /// <summary>
    /// Renders the color and face streams
    /// </summary>			
    /// <param name="nTime">timestamp of frame</param>
    /// <param name="pBuffer">pointer to frame data</param>
    /// <param name="nWidth">width (in pixels) of input image data</param>
    /// <param name="nHeight">height (in pixels) of input image data</param>
    void                   DrawStreams(INT64 nTime, RGBQUAD* pBuffer, int nWidth, int nHeight);

    /// <summary>
    /// Processes new face frames
    /// </summary>
    void                   ProcessFaces();

    /// <summary>
    /// Computes the face result text layout position by adding an offset to the corresponding 
    /// body's head joint in camera space and then by projecting it to screen space
    /// </summary>
    /// <param name="pBody">pointer to the body data</param>
    /// <param name="pFaceTextLayout">pointer to the text layout position in screen space</param>
    /// <returns>indicates success or failure</returns>
    HRESULT                GetFaceTextPositionInColorSpace(IBody* pBody, D2D1_POINT_2F* pFaceTextLayout);

    /// <summary>
    /// Updates body data
    /// </summary>
    /// <param name="ppBodies">pointer to the body data storage</param>
    /// <returns>indicates success or failure</returns>
    HRESULT                UpdateBodyData(IBody** ppBodies);

    /// <summary>
    /// Set the status bar message
    /// </summary>
    /// <param name="szMessage">message to display</param>
    /// <param name="nShowTimeMsec">time in milliseconds for which to ignore future status messages</param>
    /// <param name="bForce">force status update</param>
    /// <returns>success or failure</returns>
    bool                   SetStatusMessage(_In_z_ WCHAR* szMessage, ULONGLONG nShowTimeMsec, bool bForce);

    HWND                   m_hWnd;
    INT64                  m_nStartTime;
    INT64                  m_nLastCounter;
    double                 m_fFreq;
    ULONGLONG              m_nNextStatusTime;
    DWORD                  m_nFramesSinceUpdate;

    // Current Kinect
    IKinectSensor*         m_pKinectSensor;

    // Coordinate mapper
    ICoordinateMapper*     m_pCoordinateMapper;

    // Color reader
    IColorFrameReader*     m_pColorFrameReader;

    // Body reader
    IBodyFrameReader*      m_pBodyFrameReader;

    // Face sources
    IFaceFrameSource*	   m_pFaceFrameSources[BODY_COUNT];

    // Face readers
    IFaceFrameReader*	   m_pFaceFrameReaders[BODY_COUNT];

    // Direct2D
    ImageRenderer*         m_pDrawDataStreams;
    ID2D1Factory*          m_pD2DFactory;
    RGBQUAD*               m_pColorRGBX;	

	// ID of timer that drives audio capture.
	static const int        cAudioReadTimerId = 1;

	// Time interval, in milliseconds, for timer that drives audio capture.
	static const int        cAudioReadTimerInterval = 50;

	// Audio samples per second in Kinect audio stream
	static const int        cAudioSamplesPerSecond = 16000;

	// Number of float samples in the audio beffer we allocate for reading every time the audio capture timer fires
	// (should be larger than the amount of audio corresponding to cAudioReadTimerInterval msec).
	static const int        cAudioBufferLength = 2 * cAudioReadTimerInterval * cAudioSamplesPerSecond / 1000;

	// ID of timer that drives energy stream display.
	static const int        cEnergyRefreshTimerId = 2;

	// Time interval, in milliseconds, for timer that drives energy stream display.
	static const int        cEnergyRefreshTimerInterval = 10;

	// Number of audio samples captured from Kinect audio stream accumulated into a single
	// energy measurement that will get displayed.
	static const int        cAudioSamplesPerEnergySample = 40;

	// Number of energy samples that will be visible in display at any given time.
	static const int        cEnergySamplesToDisplay = 780;

	// Number of energy samples that will be stored in the circular buffer.
	// Always keep it higher than the energy display length to avoid overflow.
	static const int        cEnergyBufferLength = 1000;

	// Minimum energy of audio to display (in dB value, where 0 dB is full scale)
	static const int        cMinEnergy = -90;

	// To manage access to shared resources between worker thread and UI update thread
	CRITICAL_SECTION        m_csLock;

	// A single audio beam off the Kinect sensor.
	IAudioBeam*             m_pAudioBeam;

	// An IStream derived from the audio beam, used to read audio samples
	IStream*                m_pAudioStream;

	// Latest audio beam angle in radians
	float                   m_fBeamAngle;

	// Latest audio beam angle confidence, in the range [0,1]
	float                   m_fBeamAngleConfidence;

	// Buffer used to store audio stream energy data as we read audio.
	float                   m_fEnergyBuffer[cEnergyBufferLength];

	// Buffer used to store audio stream energy data ready to be displayed.
	float                   m_fEnergyDisplayBuffer[cEnergySamplesToDisplay];

	// Sum of squares of audio samples being accumulated to compute the next energy value.
	float                   m_fAccumulatedSquareSum;

	// Error between time slice we wanted to display and time slice that we ended up
	// displaying, given that we have to display in integer pixels.
	float                   m_fEnergyError;

	// Number of audio samples accumulated so far to compute the next energy value.
	int                     m_nAccumulatedSampleCount;

	// Index of next element available in audio energy buffer.
	int                     m_nEnergyIndex;

	// Number of newly calculated audio stream energy values that have not yet been displayed.
	volatile int            m_nNewEnergyAvailable;

	// Index of first energy element that has never (yet) been displayed to screen.
	int                     m_nEnergyRefreshIndex;

	// Last time energy visualization was rendered to screen.
	ULONGLONG               m_nLastEnergyRefreshTime;

	// String to store the beam and confidence for display
	wchar_t                 m_szBeamText[MAX_PATH];
};

