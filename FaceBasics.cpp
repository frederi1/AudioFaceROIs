//------------------------------------------------------------------------------
// <copyright file="FaceBasics.cpp" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------

#include "stdafx.h"
#include <strsafe.h>
#include "resource.h"
#include "FaceBasics.h"

// face property text layout offset in X axis
static const float c_FaceTextLayoutOffsetX = -0.1f;

// face property text layout offset in Y axis
static const float c_FaceTextLayoutOffsetY = -0.125f;

// define the face frame features required to be computed by this application
static const DWORD c_FaceFrameFeatures = 
    FaceFrameFeatures::FaceFrameFeatures_BoundingBoxInColorSpace
    | FaceFrameFeatures::FaceFrameFeatures_PointsInColorSpace
    | FaceFrameFeatures::FaceFrameFeatures_RotationOrientation
    | FaceFrameFeatures::FaceFrameFeatures_Happy
    | FaceFrameFeatures::FaceFrameFeatures_RightEyeClosed
    | FaceFrameFeatures::FaceFrameFeatures_LeftEyeClosed
    | FaceFrameFeatures::FaceFrameFeatures_MouthOpen
    | FaceFrameFeatures::FaceFrameFeatures_MouthMoved
    | FaceFrameFeatures::FaceFrameFeatures_LookingAway
    | FaceFrameFeatures::FaceFrameFeatures_Glasses
    | FaceFrameFeatures::FaceFrameFeatures_FaceEngagement;

/// <summary>
/// Entry point for the application
/// </summary>
/// <param name="hInstance">handle to the application instance</param>
/// <param name="hPrevInstance">always 0</param>
/// <param name="lpCmdLine">command line arguments</param>
/// <param name="nCmdShow">whether to display minimized, maximized, or normally</param>
/// <returns>status</returns>
int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
	if (SUCCEEDED(hr))
	{
		CFaceBasics application;
		application.Run(hInstance, nCmdShow);
		CoUninitialize();
	}

	return EXIT_SUCCESS;
}

/// <summary>
/// Constructor
/// </summary>
CFaceBasics::CFaceBasics() :
    m_hWnd(NULL),
    m_nStartTime(0),
    m_nLastCounter(0),
    m_nFramesSinceUpdate(0),
    m_fFreq(0),
    m_nNextStatusTime(0),
    m_pKinectSensor(nullptr),
    m_pCoordinateMapper(nullptr),
    m_pColorFrameReader(nullptr),
    m_pD2DFactory(nullptr),
    m_pDrawDataStreams(nullptr),
    m_pColorRGBX(nullptr),
    m_pBodyFrameReader(nullptr),
	m_pAudioBeam(NULL),
	m_pAudioStream(NULL),
	m_fAccumulatedSquareSum(0.0f),
	m_fEnergyError(0.0f),
	m_nAccumulatedSampleCount(0),
	m_nEnergyIndex(0),
	m_nEnergyRefreshIndex(0),
	m_nNewEnergyAvailable(0),
	m_nLastEnergyRefreshTime(NULL)
{
	InitializeCriticalSection(&m_csLock);

	ZeroMemory(m_fEnergyBuffer, sizeof(m_fEnergyBuffer));
	ZeroMemory(m_fEnergyDisplayBuffer, sizeof(m_fEnergyDisplayBuffer));
    LARGE_INTEGER qpf = {0};
    if (QueryPerformanceFrequency(&qpf))
    {
        m_fFreq = double(qpf.QuadPart);
    }

    for (int i = 0; i < BODY_COUNT; i++)
    {
        m_pFaceFrameSources[i] = nullptr;
        m_pFaceFrameReaders[i] = nullptr;
    }

    // create heap storage for color pixel data in RGBX format
    m_pColorRGBX = new RGBQUAD[cColorWidth * cColorHeight];
}


/// <summary>
/// Destructor
/// </summary>
CFaceBasics::~CFaceBasics()
{
    // clean up Direct2D renderer
    if (m_pDrawDataStreams)
    {
        delete m_pDrawDataStreams;
        m_pDrawDataStreams = nullptr;
    }

    if (m_pColorRGBX)
    {
        delete [] m_pColorRGBX;
        m_pColorRGBX = nullptr;
    }

    // clean up Direct2D
    SafeRelease(m_pD2DFactory);
	SafeRelease(m_pAudioStream);
	SafeRelease(m_pAudioBeam);

    // done with face sources and readers
    for (int i = 0; i < BODY_COUNT; i++)
    {
        SafeRelease(m_pFaceFrameSources[i]);
        SafeRelease(m_pFaceFrameReaders[i]);		
    }

    // done with body frame reader
    SafeRelease(m_pBodyFrameReader);

    // done with color frame reader
    SafeRelease(m_pColorFrameReader);

    // done with coordinate mapper
    SafeRelease(m_pCoordinateMapper);

    // close the Kinect Sensor
    if (m_pKinectSensor)
    {
        m_pKinectSensor->Close();
    }

    SafeRelease(m_pKinectSensor);

	DeleteCriticalSection(&m_csLock);
}

/// <summary>
/// Creates the main window and begins processing
/// </summary>
/// <param name="hInstance">handle to the application instance</param>
/// <param name="nCmdShow">whether to display minimized, maximized, or normally</param>
int CFaceBasics::Run(HINSTANCE hInstance, int nCmdShow)
{
    MSG       msg = {0};
    WNDCLASS  wc;

    // Dialog custom window class
    ZeroMemory(&wc, sizeof(wc));
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.cbWndExtra    = DLGWINDOWEXTRA;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP));
    wc.lpfnWndProc   = DefDlgProcW;
    wc.lpszClassName = L"FaceBasicsAppDlgWndClass";

    if (!RegisterClassW(&wc))
    {
        return 0;
    }

    // Create main application window
    HWND hWndApp = CreateDialogParamW(
        NULL,
        MAKEINTRESOURCE(IDD_APP),
        NULL,
        (DLGPROC)CFaceBasics::MessageRouter, 
        reinterpret_cast<LPARAM>(this));

    // Show window
    ShowWindow(hWndApp, nCmdShow);

    // Main message loop
    while (WM_QUIT != msg.message)
    {
        Update();

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            // If a dialog message will be taken care of by the dialog proc
            if (hWndApp && IsDialogMessageW(hWndApp, &msg))
            {
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return static_cast<int>(msg.wParam);
}

/// <summary>
/// Handles window messages, passes most to the class instance to handle
/// </summary>
/// <param name="hWnd">window message is for</param>
/// <param name="uMsg">message</param>
/// <param name="wParam">message data</param>
/// <param name="lParam">additional message data</param>
/// <returns>result of message processing</returns>
LRESULT CALLBACK CFaceBasics::MessageRouter(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CFaceBasics* pThis = nullptr;

    if (WM_INITDIALOG == uMsg)
    {
        pThis = reinterpret_cast<CFaceBasics*>(lParam);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    }
    else
    {
        pThis = reinterpret_cast<CFaceBasics*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pThis)
    {
        return pThis->DlgProc(hWnd, uMsg, wParam, lParam);
    }

    return 0;
}

/// <summary>
/// Handle windows messages for the class instance
/// </summary>
/// <param name="hWnd">window message is for</param>
/// <param name="uMsg">message</param>
/// <param name="wParam">message data</param>
/// <param name="lParam">additional message data</param>
/// <returns>result of message processing</returns>
LRESULT CALLBACK CFaceBasics::DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        {
            // Bind application window handle
            m_hWnd = hWnd;

            // Init Direct2D
            D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);

            // Create and initialize a new Direct2D image renderer (take a look at ImageRenderer.h)
            // We'll use this to draw the data we receive from the Kinect to the screen
            m_pDrawDataStreams = new ImageRenderer();
            HRESULT hr = m_pDrawDataStreams->Initialize(GetDlgItem(m_hWnd, IDC_VIDEOVIEW), m_pD2DFactory, cColorWidth, cColorHeight, cColorWidth * sizeof(RGBQUAD)); 
            if (FAILED(hr))
            {
                SetStatusMessage(L"Failed to initialize the Direct2D draw device.", 10000, true);
            }

            // Get and initialize the default Kinect sensor
            InitializeDefaultSensor();
        }
        break;

        // If the titlebar X is clicked, destroy app
    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        // Quit the main message pump
        PostQuitMessage(0);
        break;        
    }

    return FALSE;
}

/// <summary>
/// Initializes the default Kinect sensor
/// </summary>
/// <returns>S_OK on success else the failure code</returns>
HRESULT CFaceBasics::InitializeDefaultSensor()
{
    HRESULT hr;

    hr = GetDefaultKinectSensor(&m_pKinectSensor);
    if (FAILED(hr))
    {
        return hr;
    }

    if (m_pKinectSensor)
    {
        // Initialize Kinect and get color, body and face readers
        IColorFrameSource* pColorFrameSource = nullptr;
		IBodyFrameSource* pBodyFrameSource = nullptr;	
		IAudioSource* pAudioSource = NULL;
		IAudioBeamList* pAudioBeamList = NULL;

        hr = m_pKinectSensor->Open();

        if (SUCCEEDED(hr))
        {
            hr = m_pKinectSensor->get_CoordinateMapper(&m_pCoordinateMapper);
        }

        if (SUCCEEDED(hr))
        {
            hr = m_pKinectSensor->get_ColorFrameSource(&pColorFrameSource);
        }

        if (SUCCEEDED(hr))
        {
            hr = pColorFrameSource->OpenReader(&m_pColorFrameReader);
        }

        if (SUCCEEDED(hr))
        {
            hr = m_pKinectSensor->get_BodyFrameSource(&pBodyFrameSource);
        }

        if (SUCCEEDED(hr))
        {
            hr = pBodyFrameSource->OpenReader(&m_pBodyFrameReader);
        }

        if (SUCCEEDED(hr))
        {
            // create a face frame source + reader to track each body in the fov
            for (int i = 0; i < BODY_COUNT; i++)
            {
                if (SUCCEEDED(hr))
                {
                    // create the face frame source by specifying the required face frame features
                    hr = CreateFaceFrameSource(m_pKinectSensor, 0, c_FaceFrameFeatures, &m_pFaceFrameSources[i]);
                }
                if (SUCCEEDED(hr))
                {
                    // open the corresponding reader
                    hr = m_pFaceFrameSources[i]->OpenReader(&m_pFaceFrameReaders[i]);
                }				
            }
        }       
		if (SUCCEEDED(hr))
		{
			hr = m_pKinectSensor->get_AudioSource(&pAudioSource);
		}

		if (SUCCEEDED(hr))
		{
			hr = pAudioSource->get_AudioBeams(&pAudioBeamList);
		}

		if (SUCCEEDED(hr))
		{
			hr = pAudioBeamList->OpenAudioBeam(0, &m_pAudioBeam);
		}

		if (SUCCEEDED(hr))
		{
			hr = m_pAudioBeam->OpenInputStream(&m_pAudioStream);
		}

        #if 0
		// To overwrite the automatic mode of the audio beam, change it to
		// manual and set the desired beam angle. In this example, point it
		// straight forward.
		// Note that setting beam mode and beam angle will only work if the
		// application window is in the foreground. However, the operations below will
		// return S_OK even if the application window is in the background.
		// Furthermore, setting these values is an asynchronous operation --
		// it may take a short period of time for the beam to adjust.
		if (SUCCEEDED(hr))
		{
			hr = m_pAudioBeam->put_AudioBeamMode(AudioBeamMode_Manual);
		}

		if (SUCCEEDED(hr))
		{
			hr = m_pAudioBeam->put_BeamAngle(0);
		}
        #endif

		if (FAILED(hr))
		{
			SetStatusMessage(L"Failed opening an audio stream!", 10000, true);
		}

        SafeRelease(pColorFrameSource);
        SafeRelease(pBodyFrameSource);
		SafeRelease(pAudioBeamList);
		SafeRelease(pAudioSource);
    }

    if (!m_pKinectSensor || FAILED(hr))
    {
        SetStatusMessage(L"No ready Kinect found!", 10000, true);
        return E_FAIL;
    }

    return hr;
}

/// <summary>
/// Main processing function
/// </summary>
void CFaceBasics::Update()
{
    if (!m_pColorFrameReader || !m_pBodyFrameReader)
    {
        return;
    }

    IColorFrame* pColorFrame = nullptr;
    HRESULT hr = m_pColorFrameReader->AcquireLatestFrame(&pColorFrame);

    if (SUCCEEDED(hr))
    {
        INT64 nTime = 0;
        IFrameDescription* pFrameDescription = nullptr;
        int nWidth = 0;
        int nHeight = 0;
        ColorImageFormat imageFormat = ColorImageFormat_None;
        UINT nBufferSize = 0;
        RGBQUAD *pBuffer = nullptr;

        hr = pColorFrame->get_RelativeTime(&nTime);

        if (SUCCEEDED(hr))
        {
            hr = pColorFrame->get_FrameDescription(&pFrameDescription);
        }

        if (SUCCEEDED(hr))
        {
            hr = pFrameDescription->get_Width(&nWidth);
        }

        if (SUCCEEDED(hr))
        {
            hr = pFrameDescription->get_Height(&nHeight);
        }

        if (SUCCEEDED(hr))
        {
            hr = pColorFrame->get_RawColorImageFormat(&imageFormat);
        }

        if (SUCCEEDED(hr))
        {
            if (imageFormat == ColorImageFormat_Bgra)
            {
                hr = pColorFrame->AccessRawUnderlyingBuffer(&nBufferSize, reinterpret_cast<BYTE**>(&pBuffer));
            }
            else if (m_pColorRGBX)
            {
                pBuffer = m_pColorRGBX;
                nBufferSize = cColorWidth * cColorHeight * sizeof(RGBQUAD);
                hr = pColorFrame->CopyConvertedFrameDataToArray(nBufferSize, reinterpret_cast<BYTE*>(pBuffer), ColorImageFormat_Bgra);            
            }
            else
            {
                hr = E_FAIL;
            }
        }			

        if (SUCCEEDED(hr))
        {
            DrawStreams(nTime, pBuffer, nWidth, nHeight);
        }

        SafeRelease(pFrameDescription);		
    }
	ULONGLONG previousRefreshTime = m_nLastEnergyRefreshTime;
	ULONGLONG now = GetTickCount64();

	m_nLastEnergyRefreshTime = now;

	// No need to refresh if there is no new energy available to render
	if (m_nNewEnergyAvailable <= 0)
	{
		return;
	}

	{
		EnterCriticalSection(&m_csLock);

		if (previousRefreshTime != NULL)
		{
			// Calculate how many energy samples we need to advance since the last Update() call in order to
			// have a smooth animation effect.
			float energyToAdvance = m_fEnergyError + (((now - previousRefreshTime) * cAudioSamplesPerSecond / (float)1000.0) / cAudioSamplesPerEnergySample);
			int energySamplesToAdvance = min(m_nNewEnergyAvailable, (int)(energyToAdvance));
			m_fEnergyError = energyToAdvance - energySamplesToAdvance;
			m_nEnergyRefreshIndex = (m_nEnergyRefreshIndex + energySamplesToAdvance) % cEnergyBufferLength;
			m_nNewEnergyAvailable -= energySamplesToAdvance;
		}

		// Copy energy samples into buffer to be displayed, taking into account that energy
		// wraps around in a circular buffer.
		int baseIndex = (m_nEnergyRefreshIndex + cEnergyBufferLength - cEnergySamplesToDisplay) % cEnergyBufferLength;
		int samplesUntilEnd = cEnergyBufferLength - baseIndex;
		if (samplesUntilEnd>cEnergySamplesToDisplay)
		{
			memcpy_s(m_fEnergyDisplayBuffer, cEnergySamplesToDisplay*sizeof(float), m_fEnergyBuffer + baseIndex, cEnergySamplesToDisplay*sizeof(float));
		}
		else
		{
			int samplesFromBeginning = cEnergySamplesToDisplay - samplesUntilEnd;
			memcpy_s(m_fEnergyDisplayBuffer, cEnergySamplesToDisplay*sizeof(float), m_fEnergyBuffer + baseIndex, samplesUntilEnd*sizeof(float));
			memcpy_s(m_fEnergyDisplayBuffer + samplesUntilEnd, (cEnergySamplesToDisplay - samplesUntilEnd)*sizeof(float), m_fEnergyBuffer, samplesFromBeginning*sizeof(float));
		}

		LeaveCriticalSection(&m_csLock);
	}

    SafeRelease(pColorFrame);
}

/// <summary>
/// Renders the color and face streams
/// </summary>
/// <param name="nTime">timestamp of frame</param>
/// <param name="pBuffer">pointer to frame data</param>
/// <param name="nWidth">width (in pixels) of input image data</param>
/// <param name="nHeight">height (in pixels) of input image data</param>
void CFaceBasics::DrawStreams(INT64 nTime, RGBQUAD* pBuffer, int nWidth, int nHeight)
{
    if (m_hWnd)
    {
        HRESULT hr;
        hr = m_pDrawDataStreams->BeginDrawing();

        if (SUCCEEDED(hr))
        {
            // Make sure we've received valid color data
            if (pBuffer && (nWidth == cColorWidth) && (nHeight == cColorHeight))
            {
				if (m_fBeamAngleConfidence < 0.5f)
				{
					// Draw the data with Direct2D
					hr = m_pDrawDataStreams->DrawBackground(reinterpret_cast<BYTE*>(pBuffer), cColorWidth * cColorHeight * sizeof(RGBQUAD));
				}
				else
				{
					hr = m_pDrawDataStreams->SetBackground(reinterpret_cast<BYTE*>(pBuffer), cColorWidth * cColorHeight * sizeof(RGBQUAD));
				}
            }
            else
            {
                // Recieved invalid data, stop drawing
                hr = E_INVALIDARG;
            }

            if (SUCCEEDED(hr))
            {
                // begin processing the face frames
                ProcessFaces();				
            }

            m_pDrawDataStreams->EndDrawing();
        }

        if (!m_nStartTime)
        {
            m_nStartTime = nTime;
        }

        double fps = 0.0;

        LARGE_INTEGER qpcNow = {0};
        if (m_fFreq)
        {
            if (QueryPerformanceCounter(&qpcNow))
            {
                if (m_nLastCounter)
                {
                    m_nFramesSinceUpdate++;
                    fps = m_fFreq * m_nFramesSinceUpdate / double(qpcNow.QuadPart - m_nLastCounter);
                }
            }
        }

        WCHAR szStatusMessage[64];
		StringCchPrintf(szStatusMessage, _countof(szStatusMessage), L" FPS = %0.2f    Time = %I64d, Beam angle = %0.2f", fps, (nTime - m_nStartTime), 180.0f * m_fBeamAngle / static_cast<float>(M_PI));

        if (SetStatusMessage(szStatusMessage, 1000, false))
        {
            m_nLastCounter = qpcNow.QuadPart;
            m_nFramesSinceUpdate = 0;
        }
    }    
}

/// <summary>
/// Processes new face frames
/// </summary>
void CFaceBasics::ProcessFaces()
{
    HRESULT hr;
    IBody* ppBodies[BODY_COUNT] = {0};
    bool bHaveBodyData = SUCCEEDED( UpdateBodyData(ppBodies) );
	bool foundFace = false;

	if (m_fBeamAngleConfidence < 0.5f)
	{
	}
	else
	{		
		// iterate through each face reader
		for (int iFace = 0; iFace < BODY_COUNT; ++iFace)
		{
			// retrieve the latest face frame from this reader
			IFaceFrame* pFaceFrame = nullptr;
			hr = m_pFaceFrameReaders[iFace]->AcquireLatestFrame(&pFaceFrame);

			BOOLEAN bFaceTracked = false;
			if (SUCCEEDED(hr) && nullptr != pFaceFrame)
			{
				// check if a valid face is tracked in this face frame
				hr = pFaceFrame->get_IsTrackingIdValid(&bFaceTracked);
			}

			if (SUCCEEDED(hr))
			{
				if (bFaceTracked)
				{
					IFaceFrameResult* pFaceFrameResult = nullptr;
					RectI faceBox = {0};
					PointF facePoints[FacePointType::FacePointType_Count];
					Vector4 faceRotation;
					DetectionResult faceProperties[FaceProperty::FaceProperty_Count];
					D2D1_POINT_2F faceTextLayout;
					float ang;

					hr = pFaceFrame->get_FaceFrameResult(&pFaceFrameResult);

					// need to verify if pFaceFrameResult contains data before trying to access it
					if (SUCCEEDED(hr) && pFaceFrameResult != nullptr)
					{
						hr = pFaceFrameResult->get_FaceBoundingBoxInColorSpace(&faceBox);

						if (SUCCEEDED(hr))
						{										
							hr = pFaceFrameResult->GetFacePointsInColorSpace(FacePointType::FacePointType_Count, facePoints);
						}

						PointF centerMouth;
						centerMouth.X = (facePoints[3].X + facePoints[4].X) / 2;
						centerMouth.Y = (facePoints[3].Y + facePoints[4].Y) / 2;
						if (centerMouth.X < (float) 960.0)
						{
							ang = -(0.000054253472*(pow(centerMouth.X, 2)) - .10416666666666667*centerMouth.X + 50);
						}
						else
						{
							ang = (0.000054253472*(pow(centerMouth.X, 2)) - .10416666666666667*centerMouth.X + 50);
						}
						
						if (SUCCEEDED(hr))
						{
							hr = pFaceFrameResult->get_FaceRotationQuaternion(&faceRotation);
						}

						if (SUCCEEDED(hr))
						{
							hr = pFaceFrameResult->GetFaceProperties(FaceProperty::FaceProperty_Count, faceProperties);
						}

						if (SUCCEEDED(hr))
						{
							hr = GetFaceTextPositionInColorSpace(ppBodies[iFace], &faceTextLayout);
						}

						if (SUCCEEDED(hr))
						{
							if (abs((180.0f * m_fBeamAngle / static_cast<float>(M_PI)) - ang) < (float) 5.0)
							{
								foundFace = true;
								m_pDrawDataStreams->DrawFaceFrameResults(iFace, &faceBox, facePoints, &faceRotation, faceProperties, &faceTextLayout);
							}
						}							
					}

					SafeRelease(pFaceFrameResult);	
				}
				else 
				{	
					// face tracking is not valid - attempt to fix the issue
					// a valid body is required to perform this step
					if (bHaveBodyData)
					{
						// check if the corresponding body is tracked 
						// if this is true then update the face frame source to track this body
						IBody* pBody = ppBodies[iFace];
						if (pBody != nullptr)
						{
							BOOLEAN bTracked = false;
							hr = pBody->get_IsTracked(&bTracked);

							UINT64 bodyTId;
							if (SUCCEEDED(hr) && bTracked)
							{
								// get the tracking ID of this body
								hr = pBody->get_TrackingId(&bodyTId);
								if (SUCCEEDED(hr))
								{
									// update the face frame source with the tracking ID
									m_pFaceFrameSources[iFace]->put_TrackingId(bodyTId);
								}
							}
						}
					}
				}
			}	
			SafeRelease(pFaceFrame);
		}
		}
		if (!foundFace)
		{
			hr = m_pDrawDataStreams->DrawBackgroundA();
		}

		float audioBuffer[cAudioBufferLength];
		DWORD cbRead = 0;

		// S_OK will be returned when cbRead == sizeof(audioBuffer).
		// E_PENDING will be returned when cbRead < sizeof(audioBuffer).
		// For both return codes we will continue to process the audio written into the buffer.
		hr = m_pAudioStream->Read((void *)audioBuffer, sizeof(audioBuffer), &cbRead);

		if (FAILED(hr) && hr != E_PENDING)
		{
			SetStatusMessage(L"Failed to read from audio stream.", 10000, true);
		}
		else if (cbRead > 0)
		{
			DWORD nSampleCount = cbRead / sizeof(float);
			float fBeamAngle = 0.f;
			float fBeamAngleConfidence = 0.f;

			// Get most recent audio beam angle and confidence
			m_pAudioBeam->get_BeamAngle(&fBeamAngle);
			m_pAudioBeam->get_BeamAngleConfidence(&fBeamAngleConfidence);

			// Calculate energy from audio
			for (UINT i = 0; i < nSampleCount; i++)
			{
				// Compute the sum of squares of audio samples that will get accumulated
				// into a single energy value.
				m_fAccumulatedSquareSum += audioBuffer[i] * audioBuffer[i];
				++m_nAccumulatedSampleCount;

				if (m_nAccumulatedSampleCount < cAudioSamplesPerEnergySample)
				{
					continue;
				}

				// Each energy value will represent the logarithm of the mean of the
				// sum of squares of a group of audio samples.
				float fMeanSquare = m_fAccumulatedSquareSum / cAudioSamplesPerEnergySample;

				if (fMeanSquare > 1.0f)
				{
					// A loud audio source right next to the sensor may result in mean square values
					// greater than 1.0. Cap it at 1.0f for display purposes.
					fMeanSquare = 1.0f;
				}

				float fEnergy = cMinEnergy;
				if (fMeanSquare > 0.f)
				{
					// Convert to dB
					fEnergy = 10.0f*log10(fMeanSquare);
				}

			{
				// Protect shared resources with Update() method on another thread
				EnterCriticalSection(&m_csLock);

				m_fBeamAngle = fBeamAngle;
				m_fBeamAngleConfidence = fBeamAngleConfidence;

				// Renormalize signal above noise floor to [0,1] range for visualization.
				m_fEnergyBuffer[m_nEnergyIndex] = (cMinEnergy - fEnergy) / cMinEnergy;
				m_nNewEnergyAvailable++;
				m_nEnergyIndex = (m_nEnergyIndex + 1) % cEnergyBufferLength;

				LeaveCriticalSection(&m_csLock);
			}

			m_fAccumulatedSquareSum = 0.f;
			m_nAccumulatedSampleCount = 0;
			}
		}
    if (bHaveBodyData)
    {
        for (int i = 0; i < _countof(ppBodies); ++i)
        {
            SafeRelease(ppBodies[i]);
        }
    }
}

/// <summary>
/// Computes the face result text position by adding an offset to the corresponding 
/// body's head joint in camera space and then by projecting it to screen space
/// </summary>
/// <param name="pBody">pointer to the body data</param>
/// <param name="pFaceTextLayout">pointer to the text layout position in screen space</param>
/// <returns>indicates success or failure</returns>
HRESULT CFaceBasics::GetFaceTextPositionInColorSpace(IBody* pBody, D2D1_POINT_2F* pFaceTextLayout)
{
    HRESULT hr = E_FAIL;

    if (pBody != nullptr)
    {
        BOOLEAN bTracked = false;
        hr = pBody->get_IsTracked(&bTracked);

        if (SUCCEEDED(hr) && bTracked)
        {
            Joint joints[JointType_Count]; 
            hr = pBody->GetJoints(_countof(joints), joints);
            if (SUCCEEDED(hr))
            {
                CameraSpacePoint headJoint = joints[JointType_Head].Position;
                CameraSpacePoint textPoint = 
                {
                    headJoint.X + c_FaceTextLayoutOffsetX,
                    headJoint.Y + c_FaceTextLayoutOffsetY,
                    headJoint.Z
                };

                ColorSpacePoint colorPoint = {0};
                hr = m_pCoordinateMapper->MapCameraPointToColorSpace(textPoint, &colorPoint);

                if (SUCCEEDED(hr))
                {
                    pFaceTextLayout->x = colorPoint.X;
                    pFaceTextLayout->y = colorPoint.Y;
                }
            }
        }
    }

    return hr;
}

/// <summary>
/// Updates body data
/// </summary>
/// <param name="ppBodies">pointer to the body data storage</param>
/// <returns>indicates success or failure</returns>
HRESULT CFaceBasics::UpdateBodyData(IBody** ppBodies)
{
    HRESULT hr = E_FAIL;

    if (m_pBodyFrameReader != nullptr)
    {
        IBodyFrame* pBodyFrame = nullptr;
        hr = m_pBodyFrameReader->AcquireLatestFrame(&pBodyFrame);
        if (SUCCEEDED(hr))
        {
            hr = pBodyFrame->GetAndRefreshBodyData(BODY_COUNT, ppBodies);
        }
        SafeRelease(pBodyFrame);    
    }

    return hr;
}

/// <summary>
/// Set the status bar message
/// </summary>
/// <param name="szMessage">message to display</param>
/// <param name="showTimeMsec">time in milliseconds to ignore future status messages</param>
/// <param name="bForce">force status update</param>
/// <returns>success or failure</returns>
bool CFaceBasics::SetStatusMessage(_In_z_ WCHAR* szMessage, ULONGLONG nShowTimeMsec, bool bForce)
{
    ULONGLONG now = GetTickCount64();

    if (m_hWnd && (bForce || (m_nNextStatusTime <= now)))
    {
        SetDlgItemText(m_hWnd, IDC_STATUS, szMessage);
        m_nNextStatusTime = now + nShowTimeMsec;

        return true;
    }

    return false;
}

