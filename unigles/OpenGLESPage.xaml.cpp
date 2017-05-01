#include "pch.h"
#include "OpenGLESPage.xaml.h"
#include "SimpleRenderer.h"

using namespace unigles;
using namespace Platform;
using namespace Concurrency;
using namespace Windows::Foundation;
using namespace Windows::Media::Capture;

OpenGLESPage::OpenGLESPage() :
	OpenGLESPage(nullptr)
{

}

OpenGLESPage::OpenGLESPage(OpenGLES* openGLES) :
	mOpenGLES(openGLES),
	mRenderSurface(EGL_NO_SURFACE),
	mMediaCapture(nullptr),
	mCameraFrame(nullptr),
	mFrameCount(0)
{
	InitializeComponent();

    mTextureBridge = new TextureBridge();

	Windows::UI::Core::CoreWindow^ window = Windows::UI::Xaml::Window::Current->CoreWindow;

	window->VisibilityChanged +=
		ref new Windows::Foundation::TypedEventHandler<Windows::UI::Core::CoreWindow^, Windows::UI::Core::VisibilityChangedEventArgs^>(this, &OpenGLESPage::OnVisibilityChanged);

	this->Loaded +=
		ref new Windows::UI::Xaml::RoutedEventHandler(this, &OpenGLESPage::OnPageLoaded);
}

OpenGLESPage::~OpenGLESPage()
{
	StopRenderLoop();
	DestroyRenderSurface();
}

void OpenGLESPage::OnPageLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	// The SwapChainPanel has been created and arranged in the page layout, so EGL can be initialized.
	CreateRenderSurface();
	StartRenderLoop();

	InitCamera();
}

void OpenGLESPage::OnVisibilityChanged(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::VisibilityChangedEventArgs^ args)
{
	if (args->Visible && mRenderSurface != EGL_NO_SURFACE)
	{
		StartRenderLoop();
	}
	else
	{
		StopRenderLoop();
	}
}

void OpenGLESPage::CreateRenderSurface()
{
	if (mOpenGLES && mRenderSurface == EGL_NO_SURFACE)
	{
		// The app can configure the the SwapChainPanel which may boost performance.
		// By default, this template uses the default configuration.
		mRenderSurface = mOpenGLES->CreateSurface(swapChainPanel, nullptr, nullptr);

		// You can configure the SwapChainPanel to render at a lower resolution and be scaled up to
		// the swapchain panel size. This scaling is often free on mobile hardware.
		//
		// One way to configure the SwapChainPanel is to specify precisely which resolution it should render at.
		// Size customRenderSurfaceSize = Size(800, 600);
		// mRenderSurface = mOpenGLES->CreateSurface(swapChainPanel, &customRenderSurfaceSize, nullptr);
		//
		// Another way is to tell the SwapChainPanel to render at a certain scale factor compared to its size.
		// e.g. if the SwapChainPanel is 1920x1280 then setting a factor of 0.5f will make the app render at 960x640
		// float customResolutionScale = 0.5f;
		// mRenderSurface = mOpenGLES->CreateSurface(swapChainPanel, nullptr, &customResolutionScale);
		// 
	}
}

void OpenGLESPage::DestroyRenderSurface()
{
	if (mOpenGLES)
	{
		mOpenGLES->DestroySurface(mRenderSurface);
	}
	mRenderSurface = EGL_NO_SURFACE;
}

void OpenGLESPage::RecoverFromLostDevice()
{
	// Stop the render loop, reset OpenGLES, recreate the render surface
	// and start the render loop again to recover from a lost device.

	StopRenderLoop();

	{
		critical_section::scoped_lock lock(mRenderSurfaceCriticalSection);

		DestroyRenderSurface();
		mOpenGLES->Reset();
		CreateRenderSurface();
	}

	StartRenderLoop();
}

void OpenGLESPage::StartRenderLoop()
{
	// If the render loop is already running then do not start another thread.
	if (mRenderLoopWorker != nullptr && mRenderLoopWorker->Status == Windows::Foundation::AsyncStatus::Started)
	{
		return;
	}

	// Create a task for rendering that will be run on a background thread.
	auto workItemHandler = ref new Windows::System::Threading::WorkItemHandler([this](Windows::Foundation::IAsyncAction ^ action)
	{
		critical_section::scoped_lock lock(mRenderSurfaceCriticalSection);

		mOpenGLES->MakeCurrent(mRenderSurface);
		SimpleRenderer renderer;
		HANDLE cameraSurface = nullptr;

		while (action->Status == Windows::Foundation::AsyncStatus::Started)
		{
			EGLint panelWidth = 0;
			EGLint panelHeight = 0;
			mOpenGLES->GetSurfaceDimensions(mRenderSurface, &panelWidth, &panelHeight);

			{
				critical_section::scoped_lock frameLock(mFrameCriticalSection);
				if (mCameraFrame) {
					//delete cameraSurface;
					cameraSurface = mCameraFrame;
					mCameraFrame = nullptr;
				}
			}

			// Logic to update the scene could go here
			renderer.UpdateWindowSize(panelWidth, panelHeight);
            if (cameraSurface) {
                mOpenGLES->BindTexture(cameraSurface, 640, 360);
                //cameraSurface = nullptr;
            }
			renderer.Draw();

			// The call to eglSwapBuffers might not be successful (i.e. due to Device Lost)
			// If the call fails, then we must reinitialize EGL and the GL resources.
			if (mOpenGLES->SwapBuffers(mRenderSurface) != GL_TRUE)
			{
				// XAML objects like the SwapChainPanel must only be manipulated on the UI thread.
				swapChainPanel->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High, ref new Windows::UI::Core::DispatchedHandler([=]()
				{
					RecoverFromLostDevice();
				}, CallbackContext::Any));

				return;
			}
		}
	});

	// Run task on a dedicated high priority background thread.
	mRenderLoopWorker = Windows::System::Threading::ThreadPool::RunAsync(workItemHandler, Windows::System::Threading::WorkItemPriority::High, Windows::System::Threading::WorkItemOptions::TimeSliced);
}

void OpenGLESPage::StopRenderLoop()
{
	if (mRenderLoopWorker)
	{
		mRenderLoopWorker->Cancel();
		mRenderLoopWorker = nullptr;
	}
}

task<void> unigles::OpenGLESPage::InitCamera()
{
	auto groups = co_await Frames::MediaFrameSourceGroup::FindAllAsync();
	auto group = std::find_if(begin(groups), end(groups), [&](auto g) {
		return std::any_of(begin(g->SourceInfos), end(g->SourceInfos), [&](auto si) {
			return si->SourceKind == Frames::MediaFrameSourceKind::Color;
		});
	});
	if (group == end(groups)) {
		Messages->Text = L"not found";
		return;
	}
	if (!mMediaCapture.Get()) {
		try {
			mMediaCapture = ref new MediaCapture();
			co_await mMediaCapture->InitializeAsync();
		}
		catch (Exception^ ex) {
			Messages->Text = ex->ToString();
			return;
		}
	}
	auto capture = mMediaCapture.Get();
	previewPanel->Source = capture;
	Messages->Text = L"found";
	auto colorSourceInfo = std::find_if(begin((*group)->SourceInfos), end((*group)->SourceInfos), [&](auto si) {
		return si->SourceKind == Frames::MediaFrameSourceKind::Color;
	});
	auto frameSource = capture->FrameSources->Lookup((*colorSourceInfo)->Id);
	std::wostringstream dump;
	Frames::MediaFrameFormat^ preferred = nullptr;
	{
		auto format = frameSource->CurrentFormat;
		dump << "cur: " << format->VideoFormat->Width << "x" << format->VideoFormat->Height
			<< "@" << format->FrameRate->Numerator << "/" << format->FrameRate->Denominator
			<< " " << format->Subtype->Data()
			<< std::endl;
	}
	std::for_each(begin(frameSource->SupportedFormats), end(frameSource->SupportedFormats), [&](auto format) {
		if (format->FrameRate->Numerator != 30 || format->FrameRate->Denominator != 1) return;
		if (!preferred || preferred->VideoFormat->Width < format->VideoFormat->Width) {
			preferred = format;
		}
	});
	if (preferred) {
		auto format = preferred;
		dump << "Format: " << format->VideoFormat->Width << "x" << format->VideoFormat->Height
			<< "@" << format->FrameRate->Numerator << "/" << format->FrameRate->Denominator
			<< " " << format->Subtype->Data()
			<< std::endl;
		co_await frameSource->SetFormatAsync(preferred);
	}
	Messages->Text = ref new String(dump.str().c_str());
	auto reader = co_await capture->CreateFrameReaderAsync(frameSource);
	reader->FrameArrived += ref new Windows::Foundation::TypedEventHandler<Frames::MediaFrameReader^, Frames::MediaFrameArrivedEventArgs^>(this, &OpenGLESPage::OnFrameArrived);
	co_await reader->StartAsync();

	co_await capture->StartPreviewAsync();
}

void unigles::OpenGLESPage::OnFrameArrived(Windows::Media::Capture::Frames::MediaFrameReader^ sender, Windows::Media::Capture::Frames::MediaFrameArrivedEventArgs^ event)
{
	int successes = 0;
	auto frame = sender->TryAcquireLatestFrame();
	if (!frame) {
		return;
	}
	successes++;
	auto vmf = frame->VideoMediaFrame;
	std::wostringstream messageOut;
	messageOut.str(L"");
	if (vmf) {
		auto d3dSurface = vmf->Direct3DSurface;
		if (d3dSurface) {
			critical_section::scoped_lock frameWriteLock(mFrameCriticalSection);
			if (mCameraFrame) {
                mCameraFrame = nullptr;
                //return;
			}
            using namespace Windows::Graphics::DirectX::Direct3D11;
            using namespace Microsoft::WRL;
            ComPtr<IDXGISurface> nativeSurface;
            GetDXGIInterface(d3dSurface, nativeSurface.GetAddressOf());
            ComPtr<ID3D11Texture2D> surfaceTexture;
            if (FAILED(GetDXGIInterface(d3dSurface, surfaceTexture.GetAddressOf()))) {
                throw Exception::CreateException(E_FAIL, ref new Platform::String(L"Cannot cast surface"));
            }
            DXGI_SURFACE_DESC desc;
            nativeSurface->GetDesc(&desc);
            D3D11_TEXTURE2D_DESC texDesc = { 0 };
            texDesc.Width = desc.Width;
            texDesc.Height = desc.Height;
            texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            texDesc.MipLevels = 1;
            texDesc.ArraySize = 1;
            texDesc.SampleDesc.Count = 1;
            texDesc.SampleDesc.Quality = 0;
            texDesc.Usage = D3D11_USAGE_DEFAULT;
            texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            texDesc.CPUAccessFlags = 0;
            texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
            ComPtr<IDXGIDevice> device;
            if (FAILED(nativeSurface->GetDevice(__uuidof(IDXGIDevice), &device))) {
                throw Exception::CreateException(E_FAIL, ref new Platform::String(L"Failed to get DXG device"));
            }
            ComPtr<ID3D11Device> d3device;
            if (FAILED(device.As(&d3device))) {
                throw Exception::CreateException(E_FAIL, ref new Platform::String(L"Failed to get D3D device"));
            }
            ComPtr<ID3D11Texture2D> texture;
            if (FAILED(d3device->CreateTexture2D(&texDesc, nullptr, texture.GetAddressOf()))) {
                throw Exception::CreateException(E_FAIL, ref new Platform::String(L"Failed to create a texture"));
            }
            mTextureBridge->CopyAndConvert(surfaceTexture, texture);
            ComPtr<IDXGIResource1> outputResource;
            if (FAILED(texture.As(&outputResource))) {
                throw Exception::CreateException(E_FAIL, ref new Platform::String(L"Cannot cast surface"));
            }
            if (FAILED(outputResource->GetSharedHandle(&mCameraFrame))) {
                throw Exception::CreateException(E_FAIL, ref new Platform::String(L"Cannot get handle"));
            }
            return;
		}
		else {
			messageOut << "No D3D output";
		}
	}
	else {
		messageOut << "No video frame";
	}
	auto message = ref new String(messageOut.str().c_str());
	Messages->Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
		ref new Windows::UI::Core::DispatchedHandler([=]() {
		mFrameCount++;
		if (mFrameCount == 10)
		{
			Messages->Text = message;
			mFrameCount = 0;
		}
	}, CallbackContext::Any));
}