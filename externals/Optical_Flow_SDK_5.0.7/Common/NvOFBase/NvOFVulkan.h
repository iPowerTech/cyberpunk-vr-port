/*
* Copyright (c) 2022-2023 NVIDIA Corporation
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
#include "NvOF.h"

#include <vulkan/vulkan.h>
#include "nvOpticalFlowVulkan.h"

using NV_SYNC_VK = std::shared_ptr<NV_OF_SYNC_VK>;
class VkException : public std::exception
{
public:
    VkException(const std::string& errorStr)
        : m_errorString(errorStr) {}
    virtual ~VkException() {}
    virtual const char* what() const throw() { return m_errorString.c_str(); }
    static VkException makeVkException(const std::string& errorStr, VkResult result,
        const std::string& functionName, const std::string& fileName, int lineNo);

private:
    std::string m_errorString;
};

inline VkException VkException::makeVkException(const std::string& errorStr, VkResult result, const std::string& functionName,
                                        const std::string& fileName, int lineNo)
{
    std::ostringstream errorLog;
    errorLog << functionName << " : " << errorStr << " at " << fileName << ";" << lineNo << std::endl;
    VkException exception(errorLog.str());
    return exception;
}

#define VK_API_CALL(vkAPI)                                                                                  \
    do                                                                                                      \
    {                                                                                                       \
        VkResult result = vkAPI;                                                                            \
        if (result != VK_SUCCESS)                                                                           \
        {                                                                                                   \
            std::ostringstream errorLog;                                                                    \
            errorLog << #vkAPI << "returned error " << result;                                              \
            throw VkException::makeVkException(errorLog.str(), result, __FUNCTION__, __FILE__, __LINE__);   \
        }                                                                                                   \
    } while (0)

#define MAX_SUBRESOURCES 3

static const uint32_t MAX_COMMAND_BUFFFERS = 16;

/*
 * NvOFSync is a wrapper over ...
 */
class NvOFSync
{
public:
    virtual ~NvOFSync() {}
    virtual NV_SYNC_VK getVkSyncObject() { return nullptr; }
protected:
    NvOFSync()
    {
    }
   
private:
    friend class NvOF;
};

/**
* @brief A managed pointer wrapper for NvOFVk class objects
*/
class NvOFVk;

using NvOFVkObj = std::unique_ptr<NvOFVk>;

/**
* @brief A managed pointer wrapper for NvOFSync class objects
*/
using NvOFSyncObj = std::shared_ptr<NvOFSync>;

/**
* \struct NV_OF_RESOURCE_VK
* Vulkan resource.
*/
typedef struct _NV_OF_RESOURCE_VK
{
    VkImage                         image;                          /**< [in]: Vulkan image that need to be registered. */
    VkDeviceMemory                  memory;                         /**< [in]: Vulkan device memory that need to be registered. */
    uint64_t                        allocationSize;                 /**< [in]: Allocation size in bytes. */
} NV_OF_RESOURCE_VK;

/**
*  @brief Optical flow class for Vulkan resources.
*/
class NvOFVkAPI : public NvOFAPI
{
public:
    NvOFVkAPI(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);
    ~NvOFVkAPI();
    VkCommandBuffer GetCommandBuffer();
    void SubmitCommandBuffer(VkSubmitInfo* submitInfo, bool waitForQueue = false);
    void SubmitCommandBuffer(VkSubmitInfo2* submitInfo, bool waitForQueue = false);

    NV_OF_VK_API_FUNCTION_LIST* GetAPI()
    {
        //std::lock_guard<std::mutex> lock(m_lock);
        return  m_ofAPI.get();
    }

    VkDevice GetVkDevice() const { return m_device; }
    NvOFHandle GetHandle() const { return m_hOF; }
    VkDevice GetDevice() const { return m_device; }
    const VkPhysicalDeviceMemoryProperties* GetPhysicalDeviceMemoryProperties() const { return &m_memoryProperties; }
private:
    std::unique_ptr<NV_OF_VK_API_FUNCTION_LIST> m_ofAPI;
    NvOFHandle m_hOF;
    VkInstance m_instance;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;
    VkQueue m_queue;
    uint32_t m_queueFamilyIndex;
    VkPhysicalDeviceMemoryProperties m_memoryProperties;
    VkCommandPool m_cmdPool;
    VkCommandBuffer m_cmdBuffer[MAX_COMMAND_BUFFFERS];
    uint32_t m_cmdBufferIndex;
    VkFence m_fence[MAX_COMMAND_BUFFFERS];
};

template<typename RWPolicy>
class NvOFBufferVk;
/*
Vulkan Interface is different from other device interfaces. Note the private inheritance of NvOF.
*/
class NvOFVk : private NvOF
{
public:
    /*
    * @brief  This is a static function to create NvOFVk interface.
    * Returns a managed pointer to base class NvOF object.
    */
    static NvOFObj Create(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, uint32_t nWidth, uint32_t nHeight, NV_OF_BUFFER_FORMAT eInBufFmt, NV_OF_MODE eMode, NV_OF_PERF_LEVEL preset);

    virtual ~NvOFVk() {};
private:
    NvOFVk(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, 
           uint32_t nWidth, uint32_t nHeight, NV_OF_BUFFER_FORMAT eInBufFmt,
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
    virtual void DoExecute(const NV_OF_EXECUTE_INPUT_PARAMS& executeInParams,
        NV_OF_EXECUTE_OUTPUT_PARAMS& executeOutParams, void* arrWaitSyncs, uint32_t numWaitSyncs, void* pSignalSync) override;

    /**
    *  @brief This function is used to allocate buffers used for optical flow estimation.
    *  This function is an override of pure virtual function NvOF::DoAllocBuffers().
    */
    virtual  std::vector<NvOFBufferObj> DoAllocBuffers(NV_OF_BUFFER_DESCRIPTOR ofBufferDesc,
        uint32_t elementSize, uint32_t numBuffers, void* arrOutputFencePoint, uint32_t numOutputFencePoint) override;

    /**
    * @brief This function is used to register preallocated buffers.
    * This function is an override of pure virtual function NvOF::DoRegisterBuffers().
    */
    virtual  NvOFBufferObj DoRegisterBuffers(NV_OF_BUFFER_DESCRIPTOR ofBufferDesc,
        uint32_t elementSize, const void* pResource, void* pWaitSync, void* pSignalSync) override;

private:
    VkDevice                          m_vkDevice;
    std::shared_ptr<NvOFVkAPI>        m_NvOFAPI;
};

/*
NvOFBuffer Policy to provide Read and Write access to CPU and GPU
*/
class RWPolicyDeviceAndHost
{
protected:
    NvOFVkAPI*                             m_nvOFVk;
    NV_OF_RESOURCE_VK                      m_resource;
    NV_OF_BUFFER_DESCRIPTOR                m_desc;
    uint64_t                               m_totalBytes;
    VkBuffer                               m_stagingBuffer;
    VkDeviceMemory                         m_stagingBufferMem;
    uint32_t                               m_numSubResources;
    VkSubresourceLayout                    m_layout[MAX_SUBRESOURCES];
protected:
    RWPolicyDeviceAndHost(NvOFVkAPI* nvof, NV_OF_BUFFER_DESCRIPTOR desc, const NV_OF_RESOURCE_VK* pGPUResource);
    ~RWPolicyDeviceAndHost();
    void AllocateStagingBuffer(NV_OF_BUFFER_DESCRIPTOR ofBufDesc, const NV_OF_RESOURCE_VK* pGPUResource);
    void UploadData(const void* pData, void* pWaitSync, void* pSignalSync);
    void DownloadData(void* pData, void* pInputFencePoint);
};

/*
NvOFBuffer policy to avoid providing read/write access to CPU.
*/
class RWPolicyDeviceOnly
{
protected:
    RWPolicyDeviceOnly(NvOFVk* nvof, NV_OF_BUFFER_DESCRIPTOR desc, const NV_OF_RESOURCE_VK* resource) {/*do nothing*/ };
    void UploadData(const void* pData, NvOFVk* nvof, void* pInputFencePoint, void* pOutputFencePoint) { NVOF_THROW_ERROR("Invalid call. Cannot upload data from CPU for RWPolicyDeviceOnly", NV_OF_ERR_INVALID_CALL); };
    void DownloadData(void* pData, NvOFVk* nvof, void* pInputFencePoint) { NVOF_THROW_ERROR("Invalid call for RWPolicyDeviceOnly", NV_OF_ERR_INVALID_CALL); };
};

template<typename RWPolicy >
class NvOFBufferVk : private RWPolicy, public NvOFBuffer
{
public:
    ~NvOFBufferVk();
    NV_OF_RESOURCE_VK* getVkResourceHandle() { return &m_resource; }
    VkFormat getFormat() { return m_format; }
    void UploadData(const void* pData, void* pWaitSync, void* pSignalSync);
    void DownloadData(void* pData, void* pWaitSync);
    void* getAPIResourceHandle() override { return reinterpret_cast<void*>(getVkResourceHandle()->image); }
private:
    NvOFBufferVk(std::shared_ptr<NvOFVkAPI> nvof, const NV_OF_BUFFER_DESCRIPTOR& desc, uint32_t elementSize, const NV_OF_RESOURCE_VK* pResource);
    NV_OF_RESOURCE_VK m_resource;
    VkFormat m_format;
    std::shared_ptr<NvOFVkAPI> m_nvOFVk;
    friend NvOFVk;
};

struct NV_OF_CONSTANTBUFFER
{
    uint32_t srcWidth;
    uint32_t srcHeight;
    uint32_t dstWidth;
    uint32_t dstHeight;
    uint32_t nScaleFactor;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
};

class NvOFSyncVk : public NvOFSync
{
public:
    NvOFSyncVk(VkDevice device, uint64_t startValue=0);
    virtual NV_SYNC_VK getVkSyncObject() { return m_syncObject; }
    ~NvOFSyncVk();
private:
    VkDevice m_device;
    NV_SYNC_VK m_syncObject;
    friend NvOFVk;
};
