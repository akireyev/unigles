#pragma once

#include "OpenGLES.h"
#include "TextureBridge.h"
#include "OpenGLESPage.g.h"

namespace unigles {
	public ref class OpenGLESPage sealed {
	public:
		OpenGLESPage();
		virtual ~OpenGLESPage();

	internal:
		OpenGLESPage(OpenGLES* openGLES);

	private:
		void OnPageLoaded(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void OnVisibilityChanged(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::VisibilityChangedEventArgs^ args);
		void OnFrameArrived(Windows::Media::Capture::Frames::MediaFrameReader^, Windows::Media::Capture::Frames::MediaFrameArrivedEventArgs^);
		void CreateRenderSurface();
		void DestroyRenderSurface();
		void RecoverFromLostDevice();
		void StartRenderLoop();
		void StopRenderLoop();
		Concurrency::task<void> InitCamera();

		int mFrameCount;
		OpenGLES* mOpenGLES;

		EGLSurface mRenderSurface;     // This surface is associated with a swapChainPanel on the page
		Concurrency::critical_section mRenderSurfaceCriticalSection;
		Windows::Foundation::IAsyncAction^ mRenderLoopWorker;
		Platform::Agile<Windows::Media::Capture::MediaCapture> mMediaCapture;
		Concurrency::critical_section mFrameCriticalSection;
		TextureBridge* mTextureBridge;
	};
}
