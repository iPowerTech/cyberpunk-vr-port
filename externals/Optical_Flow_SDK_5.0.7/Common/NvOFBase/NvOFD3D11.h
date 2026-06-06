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


#pragma once
#include <d3d11.h>
#include <wrl.h>
#include "NvOF.h"
#include "NvOFD3DCommon.h"
#include "nvOpticalFlowD3D11.h"


/**
*  @brief Optical flow class for D3D11 resources.
*/
class NvOFD3D11API : public NvOFAPI
{
public:
    NvOFD3D11API(ID3D11DeviceContext* devContext);
    ~NvOFD3D11API();

    NV_OF_D3D11_API_FUNCTION_LIST* GetAPI()
    {
        std::lock_guard<std::mutex> lock(m_lock);
        return  m_ofAPI.get();
    }

    ID3D11DeviceContext * GetD3D11DeviceContext()
    {
        return m_deviceContext.Get();
    }

    NvOFHandle GetHandle() { return m_hOF; }
private:
    NvOFHandle m_hOF;
    std::unique_ptr<NV_OF_D3D11_API_FUNCTION_LIST> m_ofAPI;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_deviceContext;
};

class NvOFD3D11 : public NvOF
{
public:
    /**
    *  @brief This is a static function to create NvOFD3D11 interface.
    *  Returns a managed pointer to the base class NvOF.
    */
    static NvOFObj Create(ID3D11Device* d3dDevice, ID3D11DeviceContext* devContext, uint32_t nWidth, uint32_t nHeight, NV_OF_BUFFER_FORMAT eInBufFmt,
        NV_OF_MODE eMode, NV_OF_PERF_LEVEL preset);

    virtual ~NvOFD3D11() {};
private:
    NvOFD3D11(ID3D11Device* d3dDevice, ID3D11DeviceContext* devContext, uint32_t nWidth, uint32_t nHeight, NV_OF_BUFFER_FORMAT eInBufFmt,
        NV_OF_MODE eMode, NV_OF_PERF_LEVEL preset);
private:
    /**
    *  @brief This function is used to retrieve supported grid size for output.
    *  This function is an override of pure virtual function NvOF::DoGetOutputGridSizes().
    */
    virtual void DoGetOutputGridSizes(uint32_t* vals, uint32_t* size) override;

    /**
    *  @brief This function is used to retrieve if Region of Interest is supported or not.
    *  This function is an override of pure virtual function NvOF::IsROISupported().
    */
    virtual void DoGetROISupport(uint32_t* vals, uint32_t* size) override;

    /**
    *  @brief This function is used to initialize the OF engine.
    *  This function is an override of pure virtual function NvOF::DoInit().
    */
    virtual void DoInit(const NV_OF_INIT_PARAMS& initParams) override;

    /**
    *  @brief This function is used to estimate the optical flow between 2 images.
    *  This function is an override of pure virtual function NvOF::DoExecute().
    */
    virtual void DoExecute(const NV_OF_EXECUTE_INPUT_PARAMS& executeInParams, NV_OF_EXECUTE_OUTPUT_PARAMS& executeOutParams,  void* arrIputFencePoint, uint32_t numInputFencePoint, void* pOutputFencePoint = nullptr) override;

    /**
    *  @brief This function is used to allocate buffers used for optical flow estimation.
    *  This function is an override of pure virtual function NvOF::DoAllocBuffers().
    */
    virtual  std::vector<NvOFBufferObj> DoAllocBuffers(NV_OF_BUFFER_DESCRIPTOR ofBufferDesc,
        uint32_t elementSize, uint32_t numBuffers, void* arrOutputFencePoint, uint32_t numOutputFencePoint) override;

    /**
    * @brief This function is used to register preallocated buffers that can be used for optical flow estimation.
    * This function is an override of pure virtual function NvOF::DoRegisterBuffers().
    */
    virtual  NvOFBufferObj DoRegisterBuffers(NV_OF_BUFFER_DESCRIPTOR ofBufferDesc,
        uint32_t elementSize, const void* pResource, void* doNotUse , void* doNotUse2) override ;

private:
    Microsoft::WRL::ComPtr<ID3D11Device> m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_d3dDeviceContext;
    std::shared_ptr<NvOFD3D11API> m_NvOFAPI;
};

class NvOFBufferD3D11 : public NvOFBuffer
{
public:
    ~NvOFBufferD3D11();
    ID3D11Texture2D* getD3D11TextureHandle() { return m_texture.Get(); }
    ID3D11Texture2D* getD3D11StagingTextureHandle() { return m_stagingTexture.Get(); }
    DXGI_FORMAT getFormat() { return m_format; }
    void UploadData(const void* pData, void* inputFencePoint, void* outputFencePoint) override;
    void DownloadData(void* pData, void* pInputFencePoint) override;
    void SyncBuffer() override;
    void* getAPIResourceHandle() override { return reinterpret_cast<void*>(getD3D11TextureHandle()); }

private:
    NvOFBufferD3D11(std::shared_ptr<NvOFD3D11API> ofAPI, const NV_OF_BUFFER_DESCRIPTOR& desc, uint32_t elementSize, ID3D11Texture2D* pResource);
    NvOFBufferD3D11(std::shared_ptr<NvOFD3D11API> ofAPI, const NV_OF_BUFFER_DESCRIPTOR& desc, uint32_t elementSize);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_texture;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_stagingTexture;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_deviceContext;
    DXGI_FORMAT m_format;
    std::shared_ptr<NvOFD3D11API> m_nvOFAPI;
    friend class NvOFD3D11;
};
