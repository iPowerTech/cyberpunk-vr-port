/*
* Copyright (c) 2018-2023 NVIDIA Corporation
*
* Permission is hereby granted, free of charge, to any person
* obtaining a copy of this software and associated documentation
* files (the "Software"), to deal in the Software without
* restriction, including without limitation the rights to use,
* copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the software, and to permit persons to whom the
* software is furnished to do so, subject to the following
* conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
* HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*/


#include "NvOFD3D11.h"
#include "NvOFD3DCommon.h"

NvOFD3D11API::NvOFD3D11API(ID3D11DeviceContext* devContext)
    : m_deviceContext(devContext)
    , m_hOF(nullptr)
{
    typedef NV_OF_STATUS(NVOFAPI *PFNNvOFAPICreateInstanceD3D11)(uint32_t apiVer, NV_OF_D3D11_API_FUNCTION_LIST  *d3d11OF);
    PFNNvOFAPICreateInstanceD3D11 NvOFAPICreateInstanceD3D11 = (PFNNvOFAPICreateInstanceD3D11)GetProcAddress(m_hModule, "NvOFAPICreateInstanceD3D11");
    if (!NvOFAPICreateInstanceD3D11)
    {
        NVOF_THROW_ERROR("Cannot find NvOFAPICreateInstanceCuda() entry in NVOF library", NV_OF_ERR_OF_NOT_AVAILABLE);
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    m_deviceContext->GetDevice(device.GetAddressOf());
    m_ofAPI.reset(new NV_OF_D3D11_API_FUNCTION_LIST());
    NvOFAPICreateInstanceD3D11(NV_OF_API_VERSION, m_ofAPI.get());
    NV_OF_STATUS status = m_ofAPI->nvCreateOpticalFlowD3D11(device.Get(), m_deviceContext.Get(), &m_hOF);
    if (status != NV_OF_SUCCESS || m_hOF == nullptr)
    {
        NVOF_THROW_ERROR("Cannot create D3D11 optical flow device", status);
    }
}
NvOFD3D11API::~NvOFD3D11API()
{
    if (m_ofAPI)
    {
        m_ofAPI->nvOFDestroy(m_hOF);
    }
}

NvOFObj NvOFD3D11::Create(ID3D11Device* d3dDevice, ID3D11DeviceContext* devContext, uint32_t nWidth, uint32_t nHeight, NV_OF_BUFFER_FORMAT eInBufFmt,
    NV_OF_MODE eMode, NV_OF_PERF_LEVEL preset)
{
    std::unique_ptr<NvOF> ofObj(new NvOFD3D11(d3dDevice,
        devContext,
        nWidth,
        nHeight,
        eInBufFmt,
        eMode,
        preset));
    return ofObj;
}

NvOFD3D11::NvOFD3D11(ID3D11Device* d3dDevice, ID3D11DeviceContext* devContext, uint32_t nWidth, uint32_t nHeight, NV_OF_BUFFER_FORMAT eInBufFmt, NV_OF_MODE eMode,
    NV_OF_PERF_LEVEL preset)
    : NvOF(nWidth, nHeight, eInBufFmt, eMode, preset),
    m_d3dDevice(d3dDevice),
    m_d3dDeviceContext(devContext)
{

    m_NvOFAPI = std::make_shared<NvOFD3D11API>(m_d3dDeviceContext.Get());
    uint32_t formatCount = 0;
    bool bInputFormatSupported = false;
    bool bOutputFormatSupported = false;

    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFGetSurfaceFormatCountD3D11(m_NvOFAPI->GetHandle(), NV_OF_BUFFER_USAGE_INPUT, m_ofMode, &formatCount));
    std::unique_ptr<DXGI_FORMAT[]> pDxgiFormat(new DXGI_FORMAT[formatCount]);
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFGetSurfaceFormatD3D11(m_NvOFAPI->GetHandle(), NV_OF_BUFFER_USAGE_INPUT, m_ofMode, pDxgiFormat.get()));

    for (uint32_t i = 0; i < formatCount; ++i)
    {
        if (m_BufferDesc[NV_OF_BUFFER_USAGE_INPUT].bufferFormat == DXGIFormatToNvOFBufferFormat(pDxgiFormat[i]))
        {
            bInputFormatSupported = true;
        }
    }

    auto outBufFmt = (m_ofMode == NV_OF_MODE_OPTICALFLOW) ? NV_OF_BUFFER_FORMAT_SHORT2 : NV_OF_BUFFER_FORMAT_SHORT;
    formatCount = 0;
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFGetSurfaceFormatCountD3D11(m_NvOFAPI->GetHandle(), NV_OF_BUFFER_USAGE_OUTPUT, m_ofMode, &formatCount));
    pDxgiFormat.reset(new DXGI_FORMAT[formatCount]);
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFGetSurfaceFormatD3D11(m_NvOFAPI->GetHandle(), NV_OF_BUFFER_USAGE_OUTPUT, m_ofMode, pDxgiFormat.get()));

    for (uint32_t i = 0; i < formatCount; ++i)
    {
        if (outBufFmt == DXGIFormatToNvOFBufferFormat(pDxgiFormat[i]))
        {
            bOutputFormatSupported = true;
        }
    }

    if (!bOutputFormatSupported || !bInputFormatSupported)
    {
        NVOF_THROW_ERROR("Invalid buffer format", NV_OF_ERR_INVALID_PARAM);
    }
}

void NvOFD3D11::DoGetOutputGridSizes(uint32_t* vals, uint32_t* size)
{
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFGetCaps(m_NvOFAPI->GetHandle(), NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES, vals, size));
}

void NvOFD3D11::DoGetROISupport(uint32_t* vals, uint32_t* size)
{
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFGetCaps(m_NvOFAPI->GetHandle(), NV_OF_CAPS_SUPPORT_ROI, vals, size));
}

void NvOFD3D11::DoExecute(const NV_OF_EXECUTE_INPUT_PARAMS& executeInParams,  NV_OF_EXECUTE_OUTPUT_PARAMS& executeOutParams,  void* arrInputFencePoint, uint32_t numInputFencePoint, void* pOutputFencePoint)
{
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFExecute(m_NvOFAPI->GetHandle(), &executeInParams, &executeOutParams));
}

void NvOFD3D11::DoInit(const NV_OF_INIT_PARAMS& initParams)
{
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFInit(m_NvOFAPI->GetHandle(), &initParams));
}

std::vector<NvOFBufferObj>
NvOFD3D11::DoAllocBuffers(NV_OF_BUFFER_DESCRIPTOR ofBufferDesc,
    uint32_t elementSize, uint32_t numBuffers, void* arrOutputFencePoint, uint32_t numOutputFencePoint)
{
    std::vector<std::unique_ptr<NvOFBuffer>> ofBuffers;
    for (uint32_t i = 0; i < numBuffers; ++i)
    {
        ofBuffers.emplace_back(new NvOFBufferD3D11(m_NvOFAPI, ofBufferDesc, elementSize));
    }
    return ofBuffers;
}

NvOFBufferObj NvOFD3D11::DoRegisterBuffers(NV_OF_BUFFER_DESCRIPTOR ofBufferDesc,
    uint32_t elementSize, const void* pResource,  void* inputFencePoint, void* outputFencePoint)
{
    return NvOFBufferObj(new NvOFBufferD3D11(m_NvOFAPI, ofBufferDesc, elementSize, reinterpret_cast<ID3D11Texture2D*>(const_cast<void*>(pResource))));
}

NvOFBufferD3D11::NvOFBufferD3D11(std::shared_ptr<NvOFD3D11API> ofAPI, const NV_OF_BUFFER_DESCRIPTOR& desc, uint32_t elementSize, ID3D11Texture2D* pResource) :
    NvOFBuffer(desc, elementSize), m_nvOFAPI(ofAPI), m_texture(pResource)
{
     Microsoft::WRL::ComPtr<ID3D11Device> device;
     m_nvOFAPI->GetD3D11DeviceContext()->GetDevice(device.GetAddressOf());
     NVOF_API_CALL(m_nvOFAPI->GetAPI()->nvOFRegisterResourceD3D11(m_nvOFAPI->GetHandle(), pResource, &m_hGPUBuffer));
     D3D11_TEXTURE2D_DESC d3dDesc = {};
     pResource->GetDesc(&d3dDesc);
     d3dDesc.Usage = D3D11_USAGE_STAGING;
     d3dDesc.BindFlags = 0;
     d3dDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
     D3D_API_CALL(device->CreateTexture2D(&d3dDesc, NULL, &m_stagingTexture));
}

NvOFBufferD3D11::NvOFBufferD3D11(std::shared_ptr<NvOFD3D11API> ofAPI, const NV_OF_BUFFER_DESCRIPTOR& desc, uint32_t elementSize ) :
    NvOFBuffer(desc, elementSize), m_nvOFAPI(ofAPI)
{
    m_deviceContext = m_nvOFAPI->GetD3D11DeviceContext();
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    m_deviceContext->GetDevice(device.GetAddressOf());
    
    m_format = NvOFBufferFormatToDxgiFormat(desc.bufferFormat);
    D3D11_TEXTURE2D_DESC d3dBufferdesc;
    memset(&d3dBufferdesc, 0, sizeof(D3D11_TEXTURE2D_DESC));
    d3dBufferdesc.Width = desc.width;
    d3dBufferdesc.Height = desc.height;
    d3dBufferdesc.MipLevels = 1;
    d3dBufferdesc.ArraySize = 1;
    d3dBufferdesc.Format = m_format;
    d3dBufferdesc.SampleDesc.Count = 1;
    d3dBufferdesc.Usage = D3D11_USAGE_DEFAULT;
    d3dBufferdesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    d3dBufferdesc.CPUAccessFlags = 0;

    D3D_API_CALL(device->CreateTexture2D(&d3dBufferdesc, NULL, &m_texture));
    NVOF_API_CALL(m_nvOFAPI->GetAPI()->nvOFRegisterResourceD3D11(m_nvOFAPI->GetHandle(), m_texture.Get(), &m_hGPUBuffer));

    d3dBufferdesc.Usage = D3D11_USAGE_STAGING;
    d3dBufferdesc.BindFlags = 0;
    d3dBufferdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    D3D_API_CALL(device->CreateTexture2D(&d3dBufferdesc, NULL, &m_stagingTexture));
}

NvOFBufferD3D11::~NvOFBufferD3D11()
{
    m_nvOFAPI->GetAPI()->nvOFUnregisterResourceD3D11(getOFBufferHandle());
}

void NvOFBufferD3D11::UploadData(const void* pData, void* inputFencePoint, void* outputFencePoint)
{
    D3D11_MAPPED_SUBRESOURCE map;
    D3D_API_CALL(m_deviceContext->Map(m_stagingTexture.Get(), D3D11CalcSubresource(0, 0, 1), D3D11_MAP_WRITE, 0, &map));

    auto totalHeight = (m_format == DXGI_FORMAT_NV12) ? getHeight() + getHeight() / 2 : getHeight();
    for (uint32_t y = 0; y < totalHeight; y++)
    {
        memcpy((uint8_t *)map.pData + y * map.RowPitch, (uint8_t*)pData + y * getWidth() * getElementSize(),
            getWidth() * getElementSize());
    }
    m_deviceContext->Unmap(m_stagingTexture.Get(), D3D11CalcSubresource(0, 0, 1));
    m_deviceContext->CopyResource(m_texture.Get(), m_stagingTexture.Get());
}

void NvOFBufferD3D11::DownloadData(void* pData, void* pInputFencePoint)
{
    m_deviceContext->CopyResource(m_stagingTexture.Get(), m_texture.Get());
    D3D11_MAPPED_SUBRESOURCE map;
    D3D_API_CALL(m_deviceContext->Map(m_stagingTexture.Get(), D3D11CalcSubresource(0, 0, 1), D3D11_MAP_READ, 0, &map));

    auto totalHeight = (m_format == DXGI_FORMAT_NV12) ? getHeight() + getHeight() / 2 : getHeight();

    for (uint32_t y = 0; y < totalHeight; y++)
    {
        memcpy((uint8_t*)pData + y * getWidth() * getElementSize(), (uint8_t *)map.pData + y * map.RowPitch, getWidth() * getElementSize());
    }
    m_deviceContext->Unmap(m_stagingTexture.Get(), D3D11CalcSubresource(0, 0, 1));
}

void NvOFBufferD3D11::SyncBuffer()
{
    m_deviceContext->CopyResource(m_stagingTexture.Get(), m_texture.Get());
    D3D11_MAPPED_SUBRESOURCE map;
    D3D_API_CALL(m_deviceContext->Map(m_stagingTexture.Get(), D3D11CalcSubresource(0, 0, 1), D3D11_MAP_READ, 0, &map));
    m_deviceContext->Unmap(m_stagingTexture.Get(), D3D11CalcSubresource(0, 0, 1));
}
