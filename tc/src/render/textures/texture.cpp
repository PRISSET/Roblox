#include "texture.h"

#include "../../../ext/imgui/include/D3DX11tex.h"

ID3D11ShaderResourceView* D3D11CreateTextureFromBytes(
    ID3D11Device* pDevice, 
    LPCVOID pSrcData,
    SIZE_T SrcDataSize
)
{
    D3DX11_IMAGE_LOAD_INFO temp_info = {};
    ID3DX11ThreadPump* temp_pump = nullptr;
    ID3D11ShaderResourceView* pTexture = nullptr;

    HRESULT hResult;

    hResult = D3DX11CreateShaderResourceViewFromMemory(pDevice, pSrcData, SrcDataSize, &temp_info, temp_pump, &pTexture, 0);
    
    if (FAILED(hResult))
        return nullptr;

    return pTexture;
}
