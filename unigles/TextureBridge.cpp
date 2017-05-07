#include "pch.h"
#include "TextureBridge.h"

using namespace Platform;

#define STRING(s) #s
;

using namespace Windows::Graphics::DirectX::Direct3D11;
using namespace Microsoft::WRL;
using namespace DirectX;

#pragma region Locals
static inline void MustSucceed(HRESULT rc, const wchar_t* message) {
	if (FAILED(rc)) {
		throw Exception::CreateException(E_FAIL, ref new String(message));
	}
}

static inline void MustSucceed(HRESULT rc, const Microsoft::WRL::ComPtr<ID3DBlob> &error) {
	if (FAILED(rc)) {
		char *message = static_cast<char*>(error->GetBufferPointer());
		throw Exception::CreateException(E_FAIL, ref new String(L"Failed to compile shader"));
	}
}
#pragma endregion Locals

TextureBridge::TextureBridge() : mSharedTextureHandle(0), mTextureWidth(0), mTextureHeight(0) {}

TextureBridge::~TextureBridge() {}

void TextureBridge::SetupD3D(Microsoft::WRL::ComPtr<IDXGISurface> anchor) {
	if (mDevice != nullptr) {
		return;
	}
	ComPtr<IDXGIDeviceSubObject> deviceSub;
	MustSucceed(anchor.As(&deviceSub), L"Cannot cast surface");
	ComPtr<IDXGIDevice> device;
	MustSucceed(deviceSub->GetDevice(__uuidof(IDXGIDevice), &device), L"Failed to get DXG device");
	MustSucceed(device.As(&mDevice), L"Failed to cast DXG to D3D device");
	mDevice->GetImmediateContext(mDeviceContext.GetAddressOf());
	const char vertexShader[] = STRING(
		struct VS_OUTPUT {
		float4 Pos : SV_POSITION;
		float2 TexCoord : TEXCOORD;
	};
	VS_OUTPUT VS(float4 inPos : POSITION) {
		VS_OUTPUT result;
		result.Pos = inPos;
		result.TexCoord = 0.5 * (inPos.xy + float2(1, 1));
		return result;
	}
	);
	const char pixelShader[] = STRING(
		Texture2D LumTexture : register(t0);
	Texture2D ChromTexture : register(t1);
	SamplerState ObjSamplerState;

	struct VS_OUTPUT {
		float4 Pos : SV_POSITION;
		float2 TexCoord : TEXCOORD;
	};

	float4 PS(VS_OUTPUT vsData) : SV_TARGET
	{
		float lum = LumTexture.Sample(ObjSamplerState, vsData.TexCoord);
	float2 chrom = ChromTexture.Sample(ObjSamplerState, vsData.TexCoord);
	float b = 1.164 * (lum - 16.0 / 256) + 2.018 * (chrom.x - 128.0 / 256);
	float g = 1.164 * (lum - 16.0 / 256) - 0.813 * (chrom.y - 128.0 / 256) - 0.391 * (chrom.x - 128.0 / 256);
	float r = 1.164 * (lum - 16.0 / 256) + 1.596 * (chrom.y - 128.0 / 256);
	return float4(r, g, b, 1.0f);
	}
	);
	ComPtr<ID3DBlob> vsData, psData, errorData;
	MustSucceed(D3DCompile(vertexShader, sizeof(vertexShader), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, vsData.GetAddressOf(), errorData.GetAddressOf()), errorData);
	MustSucceed(D3DCompile(pixelShader, sizeof(pixelShader), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, psData.GetAddressOf(), errorData.GetAddressOf()), errorData);
	MustSucceed(mDevice->CreateVertexShader(vsData->GetBufferPointer(), vsData->GetBufferSize(), nullptr, mVertexShader.GetAddressOf()), L"Cannot create VS");
	MustSucceed(mDevice->CreatePixelShader(psData->GetBufferPointer(), psData->GetBufferSize(), nullptr, mPixelShader.GetAddressOf()), L"Cannot create PS");
	XMFLOAT3 vertices[] = {
		XMFLOAT3(1, 1, 0.5), XMFLOAT3(1, -3, 0.5), XMFLOAT3(-3, 1, 0.5)
	};
	D3D11_BUFFER_DESC vertexBufDesc = {};
	vertexBufDesc.ByteWidth = sizeof(vertices);
	vertexBufDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufDesc.Usage = D3D11_USAGE_DEFAULT;
	D3D11_SUBRESOURCE_DATA vertexBufData = {};
	vertexBufData.pSysMem = vertices;
	MustSucceed(mDevice->CreateBuffer(&vertexBufDesc, &vertexBufData, mVertexBuffer.GetAddressOf()), L"Failed to create vertex buffer");
	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	MustSucceed(mDevice->CreateSamplerState(&samplerDesc, mSamplerState.GetAddressOf()), L"Failed to create sampler state");
	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	mDevice->CreateInputLayout(layout, ARRAYSIZE(layout), vsData->GetBufferPointer(), vsData->GetBufferSize(), mInputLayout.GetAddressOf());
}

void TextureBridge::EnsureTexture(Microsoft::WRL::ComPtr<IDXGISurface> source) {
	DXGI_SURFACE_DESC desc = {};
	source->GetDesc(&desc);
	if (mTextureWidth == desc.Width && mTextureHeight == desc.Height) {
		return;
	}

	mTextureWidth = desc.Width;
	mTextureHeight = desc.Height;

	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = mTextureWidth;
	texDesc.Height = mTextureHeight;
	texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
	MustSucceed(mDevice->CreateTexture2D(&texDesc, nullptr, mSharedTexture.ReleaseAndGetAddressOf()), L"Failed to create the texture");
	ComPtr<IDXGIResource> outputResource;
	MustSucceed(mSharedTexture.As(&outputResource), L"Cannot view texture as resource");
	// TODO: Check if the handle has to be closed.
	MustSucceed(outputResource->GetSharedHandle(&mSharedTextureHandle), L"Shared texture has no handle");
}

void TextureBridge::ReadImpl(Microsoft::WRL::ComPtr<ID3D11Texture2D> source) {
	UINT stride = sizeof(XMFLOAT3);
	UINT offset = 0;
	D3D11_SHADER_RESOURCE_VIEW_DESC rvDesc = {};
	rvDesc.Format = DXGI_FORMAT_R8_UNORM;
	rvDesc.Texture2D.MipLevels = 1;
	rvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	ComPtr<ID3D11ShaderResourceView> lumResourceView, chromResourceView;
	MustSucceed(mDevice->CreateShaderResourceView(source.Get(), &rvDesc, lumResourceView.GetAddressOf()), L"Failed to create luma resource");
	rvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
	MustSucceed(mDevice->CreateShaderResourceView(source.Get(), &rvDesc, chromResourceView.GetAddressOf()), L"Failed to create chroma resource");
	mDeviceContext->VSSetShader(mVertexShader.Get(), nullptr, 0);
	mDeviceContext->PSSetShader(mPixelShader.Get(), nullptr, 0);
	mDeviceContext->PSSetShaderResources(0, 1, lumResourceView.GetAddressOf());
	mDeviceContext->PSSetShaderResources(1, 1, chromResourceView.GetAddressOf());
	mDeviceContext->PSSetSamplers(0, 1, mSamplerState.GetAddressOf());
	mDeviceContext->IASetVertexBuffers(0, 1, mVertexBuffer.GetAddressOf(), &stride, &offset);
	mDeviceContext->IASetInputLayout(mInputLayout.Get());
	mDeviceContext->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	D3D11_RENDER_TARGET_VIEW_DESC rtDesc = {};
	rtDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	ComPtr<ID3D11RenderTargetView> rtView;
	MustSucceed(mDevice->CreateRenderTargetView(mSharedTexture.Get(), &rtDesc, rtView.GetAddressOf()), L"Failed to create render target view");
	mDeviceContext->OMSetRenderTargets(1, rtView.GetAddressOf(), nullptr);
	D3D11_VIEWPORT viewport = {};
	viewport.Width = mTextureWidth;
	viewport.Height = mTextureHeight;
	mDeviceContext->RSSetViewports(1, &viewport);
	FLOAT bgColor[4] = { 1, 0, 1, 1 };
	mDeviceContext->ClearRenderTargetView(rtView.Get(), bgColor);
	mDeviceContext->Draw(3, 0);
}

void TextureBridge::ReadData(Microsoft::WRL::ComPtr<IDXGISurface> source) {
	ComPtr<ID3D11Texture2D> surfaceTexture;
	MustSucceed(source.As(&surfaceTexture), L"Source is not a texture");
	SetupD3D(source);
	EnsureTexture(source);
	ReadImpl(surfaceTexture);
}
