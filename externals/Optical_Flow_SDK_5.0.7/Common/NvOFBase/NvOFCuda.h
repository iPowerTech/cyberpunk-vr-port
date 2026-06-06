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
#include <memory>
#include "cuda.h"
#include "nvOpticalFlowCommon.h"
#include "nvOpticalFlowCuda.h"
#include "NvOF.h"
#include "NvOFUtils.h"

#define CUDA_DRVAPI_CALL( call )                                                                                                 \
    do                                                                                                                           \
    {                                                                                                                            \
        CUresult err__ = call;                                                                                                   \
        if (err__ != CUDA_SUCCESS)                                                                                               \
        {                                                                                                                        \
            const char *szErrName = NULL;                                                                                        \
            cuGetErrorName(err__, &szErrName);                                                                                   \
            std::ostringstream errorLog;                                                                                         \
            errorLog << "CUDA driver API error " << szErrName ;                                                                  \
            throw NvOFException::makeNvOFException(errorLog.str(), NV_OF_ERR_GENERIC, __FUNCTION__, __FILE__, __LINE__);         \
        }                                                                                                                        \
    }                                                                                                                            \
    while (0)


class NvOFCudaAPI : public NvOFAPI
{
public:
    NvOFCudaAPI(CUcontext cuContext, CUstream inputStream = nullptr, CUstream outputStream = nullptr);
    ~NvOFCudaAPI();

    NV_OF_CUDA_API_FUNCTION_LIST* GetAPI()
    {
        std::lock_guard<std::mutex> lock(m_lock);
        return  m_ofAPI.get();
    }

    CUcontext GetCudaContext() { return m_cuContext; }
    NvOFHandle GetHandle() { return m_hOF; }
    CUstream GetCudaStream(NV_OF_BUFFER_USAGE usage);
private:
    CUstream m_inputStream;
    CUstream m_outputStream;
    NvOFHandle m_hOF;
    std::unique_ptr<NV_OF_CUDA_API_FUNCTION_LIST> m_ofAPI;
    CUcontext m_cuContext;
};

/**
 * @brief Optical Flow for the CUDA interface
 */
class NvOFCuda : public NvOF
{
public:
    static NvOFObj Create(CUcontext cuContext, uint32_t nWidth, uint32_t nHeight,
        NV_OF_BUFFER_FORMAT eInBufFmt,
        NV_OF_CUDA_BUFFER_TYPE eInBufType,
        NV_OF_CUDA_BUFFER_TYPE eOutBufType,
        NV_OF_MODE eMode,
        NV_OF_PERF_LEVEL preset,
        CUstream inputStream = nullptr,
        CUstream outputStream = nullptr);
    ~NvOFCuda() {};

private:
    NvOFCuda(CUcontext cuContext,
        uint32_t nWidth,
        uint32_t nHeight,
        NV_OF_BUFFER_FORMAT eInBufFmt,
        NV_OF_CUDA_BUFFER_TYPE eInBufType,
        NV_OF_CUDA_BUFFER_TYPE eOutBufType,
        NV_OF_MODE eMode,
        NV_OF_PERF_LEVEL preset,
        CUstream inputStream = nullptr,
        CUstream outputStream = nullptr);
    /**
    *  @brief This function is used to retrieve supported grid size for output.
    *  This function is an override of pure virtual function NvOF::DoGetOutputGridSizes().
    */
    virtual void DoGetOutputGridSizes(uint32_t* vals, uint32_t* size) override;

    /**
    *  @brief This function is used to retrieve if Region of Interest is supported or not.
    *  This function is an override of pure virtual function NvOF::DoGetROISupport().
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
    virtual void DoExecute(const NV_OF_EXECUTE_INPUT_PARAMS& executeInParams, NV_OF_EXECUTE_OUTPUT_PARAMS& executeOutParams,  void* arrInputFencePoint, uint32_t numInputFencePoint,  void* pOutputFencePoint=nullptr) override;

    /**
    *  @brief This function is used to allocate buffers used for optical flow estimation
    *  using the cuda interface. This function is an override of pure virtual function
    *  NvOF::DoAllocBuffers().
    */
    virtual std::vector<NvOFBufferObj> DoAllocBuffers(NV_OF_BUFFER_DESCRIPTOR ofBufferDesc,
        uint32_t elementSize, uint32_t numBuffers, void* arrOutputFencePoint, uint32_t numOutputFencePoint) override;

    /**
    *  @brief This a helper function for allocating NvOFBuffer objects using the cuda
    *  interface.
    */
    std::unique_ptr<NvOFBuffer> CreateOFBufferObject(const NV_OF_BUFFER_DESCRIPTOR& desc, uint32_t elementSize, NV_OF_CUDA_BUFFER_TYPE bufferType);
    NV_OF_CUDA_BUFFER_TYPE GetBufferType(NV_OF_BUFFER_USAGE usage);
    
    /**
    * @brief This method is unsupported for NvOFCuda. 
    */
    virtual  NvOFBufferObj DoRegisterBuffers(NV_OF_BUFFER_DESCRIPTOR ofBufferDesc,
        uint32_t elementSize, const void* pResource,void* inputFencePoint, void* outputFencePoint) override;
    
private:
    CUcontext m_cuContext;
    std::shared_ptr<NvOFCudaAPI> m_NvOFAPI;
    NV_OF_CUDA_BUFFER_TYPE   m_eInBufType;
    NV_OF_CUDA_BUFFER_TYPE   m_eOutBufType;
};

/*
 * A wrapper over an NvOFGPUBufferHandle which has been created with buffer
 * type NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR.
 */
class NvOFBufferCudaDevicePtr : public NvOFBuffer
{
public:
    ~NvOFBufferCudaDevicePtr();
    CUdeviceptr getCudaDevicePtr() { return m_devPtr; }
    virtual void UploadData(const void* pData, void* inputFencePoint, void* outputFencePoint) override;
    virtual void DownloadData(void* pData, void* pInputFencePoint) override;
    NV_OF_CUDA_BUFFER_STRIDE_INFO getStrideInfo() { return m_strideInfo; }
    void* getAPIResourceHandle() override { return nullptr; }
private:
    NvOFBufferCudaDevicePtr(std::shared_ptr<NvOFCudaAPI> ofAPI, const NV_OF_BUFFER_DESCRIPTOR& desc, uint32_t elementSize);
    CUdeviceptr m_devPtr;
    CUcontext m_cuContext;
    NV_OF_CUDA_BUFFER_STRIDE_INFO m_strideInfo;
    std::shared_ptr<NvOFCudaAPI> m_NvOFAPI;
    friend class NvOFCuda;
};

/*
 * A wrapper over an NvOFGPUBufferHandle which has been created with buffer
 * type NV_OF_CUDA_BUFFER_TYPE_CUARRAY.
 */
class NvOFBufferCudaArray : public NvOFBuffer
{
public:
    ~NvOFBufferCudaArray();
    virtual void UploadData(const void* pData, void* inputFencePoint, void* outputFencePoint) override;
    virtual void DownloadData(void* pData, void* pInputFencePoint) override;
    CUarray getCudaArray() { return m_cuArray; }
    void* getAPIResourceHandle() override { return nullptr; }
private:
    NvOFBufferCudaArray(std::shared_ptr<NvOFCudaAPI> ofAPI, const NV_OF_BUFFER_DESCRIPTOR& desc, uint32_t elementSize);
    CUarray m_cuArray;
    CUcontext m_cuContext;
    NV_OF_CUDA_BUFFER_STRIDE_INFO m_strideInfo;
    std::shared_ptr<NvOFCudaAPI> m_NvOFAPI;
    friend class NvOFCuda;
};

class NvOFUtilsCuda : public NvOFUtils
{
public:
    NvOFUtilsCuda(NV_OF_MODE eMode);
    virtual void Upsample(NvOFBuffer *srcBuffer, NvOFBuffer *dstBuffer, uint32_t nScaleFactor) override;
};

