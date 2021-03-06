#pragma once

class OpenGLES {
public:
	OpenGLES();
	~OpenGLES();

	EGLSurface CreateSurface(Windows::UI::Xaml::Controls::SwapChainPanel^ panel, const Windows::Foundation::Size* renderSurfaceSize, const float* renderResolutionScale);
	void GetSurfaceDimensions(const EGLSurface surface, EGLint *width, EGLint *height);
	void DestroySurface(const EGLSurface surface);
	void MakeCurrent(const EGLSurface surface);
	EGLBoolean SwapBuffers(const EGLSurface surface);
	void BindCameraSurface(HANDLE texture, int width, int height);
	void Reset();

private:
	void Initialize();
	void Cleanup();

private:
	EGLDisplay mEglDisplay;
	EGLContext mEglContext;
	EGLConfig  mEglConfig;

	EGLSurface mCameraSurface;
	HANDLE mCameraTextureHandle;
	UINT mCameraWidth, mCameraHeight;
};
