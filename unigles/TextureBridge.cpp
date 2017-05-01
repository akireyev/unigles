#include "pch.h"
#include "TextureBridge.h"

using namespace Platform;

#define STRING(s) #s

TextureBridge::TextureBridge() {
}


TextureBridge::~TextureBridge() {
}

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

void TextureBridge::CopyAndConvert(Microsoft::WRL::ComPtr<ID3D11Texture2D> src, Microsoft::WRL::ComPtr<ID3D11Texture2D> dest) {
    const char vertexShader[] = STRING(
    struct VS_OUTPUT {
        float4 Pos : SV_POSITION;
        float2 TexCoord : TEXCOORD;
    };
    VS_OUTPUT VS(float4 inPos : POSITION)
    {
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
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    UINT numElements = ARRAYSIZE(layout);
    using namespace Windows::Graphics::DirectX::Direct3D11;
    using namespace Microsoft::WRL;
    using namespace DirectX;
    ComPtr<IDXGIDeviceSubObject> deviceSub;
    MustSucceed(src.As(&deviceSub), L"Cannot cast surface");
    ComPtr<IDXGIDevice> device;
    MustSucceed(deviceSub->GetDevice(__uuidof(IDXGIDevice), &device), L"Failed to get DXG device");
    ComPtr<ID3D11Device> d3device;
    MustSucceed(device.As(&d3device), L"Failed to get D3D device");
    ComPtr<ID3D11DeviceContext> context;
    d3device->GetImmediateContext(context.GetAddressOf());
    ComPtr<ID3DBlob> vsData, psData, errorData;
    MustSucceed(D3DCompile(vertexShader, sizeof(vertexShader), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, vsData.GetAddressOf(), errorData.GetAddressOf()), errorData);
    MustSucceed(D3DCompile(pixelShader, sizeof(pixelShader), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, psData.GetAddressOf(), errorData.GetAddressOf()), errorData);
    ComPtr<ID3D11VertexShader> vs;
    MustSucceed(d3device->CreateVertexShader(vsData->GetBufferPointer(), vsData->GetBufferSize(), nullptr, vs.GetAddressOf()), L"Cannot create VS");
    ComPtr<ID3D11PixelShader> ps;
    MustSucceed(d3device->CreatePixelShader(psData->GetBufferPointer(), psData->GetBufferSize(), nullptr, ps.GetAddressOf()), L"Cannot create PS");
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->PSSetShader(ps.Get(), nullptr, 0);
    XMFLOAT3 vertices[] = {
        XMFLOAT3(1, 1, 0.5), XMFLOAT3(1, -3, 0.5), XMFLOAT3(-3, 1, 0.5)
    };
    D3D11_BUFFER_DESC vertexBufDesc = {};
    vertexBufDesc.ByteWidth = sizeof(vertices);
    vertexBufDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertexBufDesc.Usage = D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA vertexBufData = {};
    vertexBufData.pSysMem = vertices;
    ComPtr<ID3D11Buffer> vertexBuffer;
    MustSucceed(d3device->CreateBuffer(&vertexBufDesc, &vertexBufData, vertexBuffer.GetAddressOf()), L"Failed to create vertex buffer");
    UINT stride = sizeof(XMFLOAT3);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
    ComPtr<ID3D11InputLayout> vertLayout;
    d3device->CreateInputLayout(layout, ARRAYSIZE(layout), vsData->GetBufferPointer(), vsData->GetBufferSize(), vertLayout.GetAddressOf());
    context->IASetInputLayout(vertLayout.Get());
    context->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D11_SHADER_RESOURCE_VIEW_DESC rvDesc = {};
    rvDesc.Format = DXGI_FORMAT_R8_UNORM;
    rvDesc.Texture2D.MipLevels = 1;
    rvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11ShaderResourceView> lumResourceView, chromResourceView;
    MustSucceed(d3device->CreateShaderResourceView(src.Get(), &rvDesc, lumResourceView.GetAddressOf()), L"Failed to create luma resource");
    rvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    MustSucceed(d3device->CreateShaderResourceView(src.Get(), &rvDesc, chromResourceView.GetAddressOf()), L"Failed to create chroma resource");
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> samplerState;
    MustSucceed(d3device->CreateSamplerState(&samplerDesc, samplerState.GetAddressOf()), L"Failed to create sampler state");
    context->PSSetShaderResources(0, 1, lumResourceView.GetAddressOf());
    context->PSSetShaderResources(1, 1, chromResourceView.GetAddressOf());
    context->PSSetSamplers(0, 1, samplerState.GetAddressOf());
    D3D11_RENDER_TARGET_VIEW_DESC rtDesc = {};
    rtDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rtDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> rtView;
    MustSucceed(d3device->CreateRenderTargetView(dest.Get(), &rtDesc, rtView.GetAddressOf()), L"Failed to create render target view");
    context->OMSetRenderTargets(1, rtView.GetAddressOf(), nullptr);
    D3D11_VIEWPORT viewport = {};
    viewport.Width = 640;
    viewport.Height = 360;
    context->RSSetViewports(1, &viewport);
    FLOAT bgColor[4] = { 1, 0, 1, 1 };
    context->ClearRenderTargetView(rtView.Get(), bgColor);
    context->Draw(3, 0);
    //ComPtr<ID3D11Resource> sr, dr;
    //src.As(&sr);
    //dest.As(&dr);
    //context->CopyResource(dr.Get(), sr.Get());
}
