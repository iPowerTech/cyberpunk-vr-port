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


#ifndef _WIN32
#include <dlfcn.h>
#endif
#include "NvOFCuda.h"

NvOFCudaAPI::NvOFCudaAPI(CUcontext cuContext, CUstream inputStream, CUstream outputStream)
    : m_cuContext(cuContext), m_inputStream(inputStream), m_outputStream(outputStream)
{
    typedef NV_OF_STATUS(NVOFAPI *PFNNvOFAPICreateInstanceCuda)(uint32_t apiVer, NV_OF_CUDA_API_FUNCTION_LIST* cudaOf);
#if defined(_WIN32)
    PFNNvOFAPICreateInstanceCuda NvOFAPICreateInstanceCuda = (PFNNvOFAPICreateInstanceCuda)GetProcAddress(m_hModule, "NvOFAPICreateInstanceCuda");
#else
    PFNNvOFAPICreateInstanceCuda NvOFAPICreateInstanceCuda = (PFNNvOFAPICreateInstanceCuda)dlsym(m_hModule, "NvOFAPICreateInstanceCuda");
#endif
    if (!NvOFAPICreateInstanceCuda)
    {
        NVOF_THROW_ERROR("Cannot find NvOFAPICreateInstanceCuda() entry in NVOF library", NV_OF_ERR_OF_NOT_AVAILABLE);
    }

    m_ofAPI.reset(new NV_OF_CUDA_API_FUNCTION_LIST());

    NVOF_API_CALL(NvOFAPICreateInstanceCuda(NV_OF_API_VERSION, m_ofAPI.get()));
    NVOF_API_CALL(m_ofAPI->nvCreateOpticalFlowCuda(m_cuContext, &m_hOF));
    NVOF_API_CALL(m_ofAPI->nvOFSetIOCudaStreams(m_hOF, m_inputStream, m_outputStream));
}

NvOFCudaAPI::~NvOFCudaAPI()
{
    if (m_ofAPI)
    {
        m_ofAPI->nvOFDestroy(m_hOF);
    }
}

CUstream NvOFCudaAPI::GetCudaStream(NV_OF_BUFFER_USAGE usage)
{
    CUstream stream = 0;
    if (usage == NV_OF_BUFFER_USAGE_INPUT)
    {
        stream = m_inputStream;
    }
    else if ((usage == NV_OF_BUFFER_USAGE_OUTPUT) ||
        (usage == NV_OF_BUFFER_USAGE_COST) ||
        (usage == NV_OF_BUFFER_USAGE_HINT))
    {
        stream = m_outputStream;
    }
    return stream;
}

NvOFObj NvOFCuda::Create(CUcontext cuContext, uint32_t nWidth, uint32_t nHeight,
    NV_OF_BUFFER_FORMAT eInBufFmt,
    NV_OF_CUDA_BUFFER_TYPE eInBufType,
    NV_OF_CUDA_BUFFER_TYPE eOutBufType,
    NV_OF_MODE eMode,
    NV_OF_PERF_LEVEL preset,
    CUstream inputStream,
    CUstream outputStream)
{
    std::unique_ptr<NvOF> ofObj(new NvOFCuda(cuContext,
        nWidth,
        nHeight,
        eInBufFmt,
        eInBufType,
        eOutBufType,
        eMode,
        preset,
        inputStream,
        outputStream));
    return ofObj;
}

NvOFCuda::NvOFCuda(CUcontext cuContext,
    uint32_t nWidth,
    uint32_t nHeight,
    NV_OF_BUFFER_FORMAT eInBufFmt,
    NV_OF_CUDA_BUFFER_TYPE eInBufType,
    NV_OF_CUDA_BUFFER_TYPE eOutBufType,
    NV_OF_MODE eMode,
    NV_OF_PERF_LEVEL preset,
    CUstream inputStream,
    CUstream outputStream)
: NvOF(nWidth, nHeight, eInBufFmt, eMode, preset),
  m_cuContext(cuContext),
  m_eInBufType(eInBufType),
  m_eOutBufType(eOutBufType)
{
    m_NvOFAPI = std::make_shared<NvOFCudaAPI>(m_cuContext, inputStream, outputStream);
}

void NvOFCuda::DoGetOutputGridSizes(uint32_t* vals, uint32_t* size)
{
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFGetCaps(m_NvOFAPI->GetHandle(), NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES, vals, size));
}

void NvOFCuda::DoGetROISupport(uint32_t* vals, uint32_t* size)
{
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFGetCaps(m_NvOFAPI->GetHandle(), NV_OF_CAPS_SUPPORT_ROI, vals, size));
}

void NvOFCuda::DoExecute(const NV_OF_EXECUTE_INPUT_PARAMS& executeInParams, NV_OF_EXECUTE_OUTPUT_PARAMS& executeOutParams, void* pInputFencePoint, uint32_t numFencePoint,  void* pOutputFencePoint )
{
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFExecute(m_NvOFAPI->GetHandle(), &executeInParams, &executeOutParams));
}

void NvOFCuda::DoInit(const NV_OF_INIT_PARAMS& initParams)
{
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFInit(m_NvOFAPI->GetHandle(), &initParams));
}

NvOFBufferObj NvOFCuda::DoRegisterBuffers(NV_OF_BUFFER_DESCRIPTOR ofBufferDesc,
        uint32_t elementSize, const void* pResource,void* inputFencePoint, void* outputFencePoint)
{
    NVOF_THROW_ERROR("Cannot register preallocated buffers with NvOFCuda", NV_OF_ERR_INVALID_CALL);
    return NvOFBufferObj();
}

NV_OF_CUDA_BUFFER_TYPE NvOFCuda::GetBufferType(NV_OF_BUFFER_USAGE usage)
{
    NV_OF_CUDA_BUFFER_TYPE bufferType = NV_OF_CUDA_BUFFER_TYPE_UNDEFINED;
    if (usage == NV_OF_BUFFER_USAGE_INPUT)
    {
        bufferType = m_eInBufType;
    }
    else if ((usage  == NV_OF_BUFFER_USAGE_OUTPUT) || 
            (usage == NV_OF_BUFFER_USAGE_COST)   ||
            (usage == NV_OF_BUFFER_USAGE_HINT))
    {
        bufferType = m_eOutBufType;
    }

    return bufferType;
}

std::vector<NvOFBufferObj>
NvOFCuda::DoAllocBuffers(NV_OF_BUFFER_DESCRIPTOR ofBufferDesc,
    uint32_t elementSize, uint32_t numBuffers, void* arrOutputFencePoint, uint32_t numOutputFencePoint)
{
    std::vector<NvOFBufferObj> ofBuffers;
    for (uint32_t i = 0; i < numBuffers; ++i)
    {
        NV_OF_CUDA_BUFFER_TYPE bufferType = GetBufferType(ofBufferDesc.bufferUsage);
        ofBuffers.emplace_back(CreateOFBufferObject(ofBufferDesc, elementSize, bufferType).release());
    }
    return ofBuffers;
}

std::unique_ptr<NvOFBuffer> NvOFCuda::CreateOFBufferObject(const NV_OF_BUFFER_DESCRIPTOR& desc, uint32_t elementSize, NV_OF_CUDA_BUFFER_TYPE bufferType)
{
    std::unique_ptr<NvOFBuffer> pBuffer;
    if (bufferType == NV_OF_CUDA_BUFFER_TYPE_CUARRAY)
    {
        pBuffer.reset(new NvOFBufferCudaArray(m_NvOFAPI, desc, elementSize));
    }
    else
    {
        pBuffer.reset(new NvOFBufferCudaDevicePtr(m_NvOFAPI, desc, elementSize));
    }
    return pBuffer;
}

NvOFBufferCudaDevicePtr::NvOFBufferCudaDevicePtr(std::shared_ptr<NvOFCudaAPI> ofAPI, const NV_OF_BUFFER_DESCRIPTOR& desc, uint32_t elementSize) :
    NvOFBuffer(desc, elementSize), m_NvOFAPI(ofAPI), m_devPtr(0)
{
    m_cuContext = m_NvOFAPI->GetCudaContext();
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFCreateGPUBufferCuda(m_NvOFAPI->GetHandle(),
        &desc,
        NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR,
        &m_hGPUBuffer));
    m_devPtr = m_NvOFAPI->GetAPI()->nvOFGPUBufferGetCUdeviceptr(m_hGPUBuffer);
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFGPUBufferGetStrideInfo(m_hGPUBuffer, &m_strideInfo));
}

NvOFBufferCudaDevicePtr::~NvOFBufferCudaDevicePtr()
{
    m_NvOFAPI->GetAPI()->nvOFDestroyGPUBufferCuda(m_hGPUBuffer);
}

void NvOFBufferCudaDevicePtr::UploadData(const void *pData, void* inputFencePoint, void* outputFencePoint)
{
    CUstream stream = m_NvOFAPI->GetCudaStream(getBufferUsage());
    CUDA_DRVAPI_CALL(cuCtxPushCurrent(m_cuContext));
    CUDA_MEMCPY2D cuCopy2d;
    memset(&cuCopy2d, 0, sizeof(cuCopy2d));
    cuCopy2d.WidthInBytes = getWidth()* getElementSize();
    cuCopy2d.srcMemoryType = CU_MEMORYTYPE_HOST;
    cuCopy2d.srcHost = pData;
    cuCopy2d.srcPitch = cuCopy2d.WidthInBytes;
    cuCopy2d.dstMemoryType = CU_MEMORYTYPE_DEVICE;
    cuCopy2d.dstDevice = getCudaDevicePtr();
    cuCopy2d.dstPitch = m_strideInfo.strideInfo[0].strideXInBytes;
    cuCopy2d.Height   = getHeight();
    CUDA_DRVAPI_CALL(cuMemcpy2DAsync(&cuCopy2d, stream));

    if (getBufferFormat() == NV_OF_BUFFER_FORMAT_NV12)
    {
        cuCopy2d.Height   = (getHeight() + 1)/2;
        cuCopy2d.srcHost  = ((const uint8_t *)pData + (cuCopy2d.srcPitch * cuCopy2d.Height));
        cuCopy2d.dstY     = m_strideInfo.strideInfo[0].strideYInBytes;
        CUDA_DRVAPI_CALL(cuMemcpy2DAsync(&cuCopy2d, stream));
    }
    CUDA_DRVAPI_CALL(cuCtxPopCurrent(&m_cuContext));
}

void NvOFBufferCudaDevicePtr::DownloadData(void *pData, void* pInputFencePoint)
{
    CUstream stream = m_NvOFAPI->GetCudaStream(getBufferUsage());
    CUDA_DRVAPI_CALL(cuCtxPushCurrent(m_cuContext));
    CUDA_MEMCPY2D cuCopy2d;
    memset(&cuCopy2d, 0, sizeof(cuCopy2d));
    cuCopy2d.WidthInBytes = getWidth() * getElementSize();

    cuCopy2d.dstMemoryType = CU_MEMORYTYPE_HOST;
    cuCopy2d.dstHost = pData;
    cuCopy2d.dstPitch = cuCopy2d.WidthInBytes;
    cuCopy2d.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    cuCopy2d.srcDevice = getCudaDevicePtr();
    cuCopy2d.srcPitch = m_strideInfo.strideInfo[0].strideXInBytes;
    cuCopy2d.Height = getBufferFormat() == NV_OF_BUFFER_FORMAT_NV12 ? (getHeight() + getHeight() /2) : getHeight();
    CUDA_DRVAPI_CALL(cuMemcpy2DAsync(&cuCopy2d, stream));
    if (getBufferFormat() == NV_OF_BUFFER_FORMAT_NV12)
    {
        cuCopy2d.Height = (getHeight() + 1) / 2;
        cuCopy2d.dstHost = ((uint8_t *)pData + (cuCopy2d.dstPitch * cuCopy2d.Height));
        cuCopy2d.srcY = m_strideInfo.strideInfo[0].strideYInBytes;
        CUDA_DRVAPI_CALL(cuMemcpy2DAsync(&cuCopy2d, stream));
    }
    CUDA_DRVAPI_CALL(cuStreamSynchronize(stream));
    CUDA_DRVAPI_CALL(cuCtxPopCurrent(&m_cuContext));
}

NvOFBufferCudaArray::NvOFBufferCudaArray(std::shared_ptr<NvOFCudaAPI> ofAPI, const NV_OF_BUFFER_DESCRIPTOR& desc, uint32_t elementSize) :
    NvOFBuffer(desc, elementSize), m_NvOFAPI(ofAPI), m_cuArray(0)
{
    m_cuContext = m_NvOFAPI->GetCudaContext();
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFCreateGPUBufferCuda(m_NvOFAPI->GetHandle(),
        &desc,
        NV_OF_CUDA_BUFFER_TYPE_CUARRAY,
        &m_hGPUBuffer));
    m_cuArray = m_NvOFAPI->GetAPI()->nvOFGPUBufferGetCUarray(m_hGPUBuffer);
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFGPUBufferGetStrideInfo(m_hGPUBuffer, &m_strideInfo));
}

NvOFBufferCudaArray::~NvOFBufferCudaArray()
{
    m_NvOFAPI->GetAPI()->nvOFDestroyGPUBufferCuda(m_hGPUBuffer);
}

void NvOFBufferCudaArray::UploadData(const void *pData, void* inputFencePoint, void* outputFencePoint)
{
    CUstream stream = m_NvOFAPI->GetCudaStream(getBufferUsage());
    CUDA_DRVAPI_CALL(cuCtxPushCurrent(m_cuContext));
    CUDA_MEMCPY2D cuCopy2d;
    memset(&cuCopy2d, 0, sizeof(cuCopy2d));
    cuCopy2d.WidthInBytes = getWidth()* getElementSize();
    cuCopy2d.srcMemoryType = CU_MEMORYTYPE_HOST;
    cuCopy2d.srcHost = pData;
    cuCopy2d.srcPitch = cuCopy2d.WidthInBytes;
    cuCopy2d.dstMemoryType = CU_MEMORYTYPE_ARRAY;
    cuCopy2d.dstArray= getCudaArray();
    cuCopy2d.Height = getHeight();
    CUDA_DRVAPI_CALL(cuMemcpy2DAsync(&cuCopy2d, stream));

    if (getBufferFormat() == NV_OF_BUFFER_FORMAT_NV12)
    {
        cuCopy2d.Height = (getHeight() + 1) / 2;
        cuCopy2d.srcHost = ((const uint8_t *)pData + (cuCopy2d.srcPitch * cuCopy2d.Height));
        cuCopy2d.dstY = m_strideInfo.strideInfo[0].strideYInBytes;
        CUDA_DRVAPI_CALL(cuMemcpy2DAsync(&cuCopy2d, stream));
    }
    CUDA_DRVAPI_CALL(cuCtxPopCurrent(&m_cuContext));
}

void NvOFBufferCudaArray::DownloadData(void *pData, void* pInputFencePoint)
{
    CUstream stream = m_NvOFAPI->GetCudaStream(getBufferUsage());
    CUDA_DRVAPI_CALL(cuCtxPushCurrent(m_cuContext));
    CUDA_MEMCPY2D cuCopy2d;
    memset(&cuCopy2d, 0, sizeof(cuCopy2d));
    cuCopy2d.WidthInBytes = getWidth() * getElementSize();

    cuCopy2d.dstMemoryType = CU_MEMORYTYPE_HOST;
    cuCopy2d.dstHost = pData;
    cuCopy2d.dstPitch = cuCopy2d.WidthInBytes;
    cuCopy2d.srcMemoryType = CU_MEMORYTYPE_ARRAY;
    cuCopy2d.srcArray = getCudaArray();
    cuCopy2d.Height = getBufferFormat() == NV_OF_BUFFER_FORMAT_NV12 ? (getHeight() + getHeight() / 2) : getHeight();
    CUDA_DRVAPI_CALL(cuMemcpy2DAsync(&cuCopy2d, stream));
    if (getBufferFormat() == NV_OF_BUFFER_FORMAT_NV12)
    {
        cuCopy2d.Height = (getHeight() + 1) / 2;
        cuCopy2d.dstHost = ((uint8_t *)pData + (cuCopy2d.dstPitch * cuCopy2d.Height));
        cuCopy2d.srcY = m_strideInfo.strideInfo[0].strideYInBytes;
        CUDA_DRVAPI_CALL(cuMemcpy2DAsync(&cuCopy2d, stream));
    }
    CUDA_DRVAPI_CALL(cuStreamSynchronize(stream));
    CUDA_DRVAPI_CALL(cuCtxPopCurrent(&m_cuContext));
}

