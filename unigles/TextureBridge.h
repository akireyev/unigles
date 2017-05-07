#pragma once
class TextureBridge {
public:
	TextureBridge();
	virtual ~TextureBridge();

	void ReadData(Microsoft::WRL::ComPtr<IDXGISurface> source);
	HANDLE GetTextureHandle() const { return mSharedTextureHandle; }
	UINT GetTextureWidth() const { return mTextureWidth; }
	UINT GetTextureHeight() const { return mTextureHeight; }

private:
	Microsoft::WRL::ComPtr<ID3D11Texture2D> mSharedTexture;
	Microsoft::WRL::ComPtr<ID3D11Device> mDevice;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> mDeviceContext;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> mVertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> mPixelShader;
	Microsoft::WRL::ComPtr<ID3D11InputLayout> mInputLayout;
	Microsoft::WRL::ComPtr<ID3D11Buffer> mVertexBuffer;
	Microsoft::WRL::ComPtr<ID3D11SamplerState> mSamplerState;

	HANDLE mSharedTextureHandle;
	UINT mTextureWidth, mTextureHeight;

	void SetupD3D(Microsoft::WRL::ComPtr<IDXGISurface> anchor);
	void EnsureTexture(Microsoft::WRL::ComPtr<IDXGISurface> source);
	void ReadImpl(Microsoft::WRL::ComPtr<ID3D11Texture2D> source);
};
