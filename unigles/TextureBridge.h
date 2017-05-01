#pragma once
class TextureBridge {
public:
    TextureBridge();
    virtual ~TextureBridge();

    void CopyAndConvert(Microsoft::WRL::ComPtr<ID3D11Texture2D> src, Microsoft::WRL::ComPtr<ID3D11Texture2D> dest);
};

