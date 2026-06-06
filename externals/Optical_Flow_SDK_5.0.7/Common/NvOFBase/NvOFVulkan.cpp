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


#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <assert.h>
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include "NvOFVulkan.h"
#include "NvOFUtilsVulkan.h"

using namespace GNvOFUtilsVulkan;

NvOFVkAPI::NvOFVkAPI(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device)
    : m_instance(instance)
    , m_physicalDevice(physicalDevice)
    , m_device(device)
    , m_queue{}
    , m_cmdPool(VK_NULL_HANDLE)
    , m_cmdBuffer{}
    , m_cmdBufferIndex(0)
    , m_fence{}
    , m_hOF(NULL)
    , m_queueFamilyIndex{0}
    , m_memoryProperties{0}
{
    typedef NV_OF_STATUS(NVOFAPI* PFNNvOFAPICreateInstanceVk)(uint32_t apiVer, NV_OF_VK_API_FUNCTION_LIST* pFuncList);
    #if defined (_WIN32)
        PFNNvOFAPICreateInstanceVk NvOFAPICreateInstanceVk = (PFNNvOFAPICreateInstanceVk)GetProcAddress(m_hModule, "NvOFAPICreateInstanceVk");
    #else
        PFNNvOFAPICreateInstanceVk NvOFAPICreateInstanceVk = (PFNNvOFAPICreateInstanceVk)dlsym(m_hModule, "NvOFAPICreateInstanceVk");
    #endif
    if (!NvOFAPICreateInstanceVk)
    {
        NVOF_THROW_ERROR("Cannot find NvOFAPICreateInstanceVk() entry in NVOF library", NV_OF_ERR_OF_NOT_AVAILABLE);
    }

    m_ofAPI.reset(new NV_OF_VK_API_FUNCTION_LIST());
    NV_OF_STATUS status = NvOFAPICreateInstanceVk(NV_OF_API_VERSION, m_ofAPI.get());
    if (status != NV_OF_SUCCESS)
    {
        NVOF_THROW_ERROR("Cannot fetch function list", status);
    }
    status = m_ofAPI->nvCreateOpticalFlowVk(m_instance, m_physicalDevice, m_device, &m_hOF);
    if (status != NV_OF_SUCCESS || m_hOF == nullptr)
    {
        NVOF_THROW_ERROR("Cannot create Vulkan optical flow device", status);
    }

    VkQueueFlags requiredCaps = VK_QUEUE_TRANSFER_BIT;

    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, NULL);
    std::unique_ptr<VkQueueFamilyProperties[]> queueProps(new VkQueueFamilyProperties[queueFamilyCount]);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueProps.get());

    for (m_queueFamilyIndex = 0; m_queueFamilyIndex < queueFamilyCount; m_queueFamilyIndex++) {
        if ((queueProps[m_queueFamilyIndex].queueFlags & requiredCaps) == requiredCaps) {
            break;
        }
    }

    if (m_queueFamilyIndex == queueFamilyCount) {
        NVOF_THROW_ERROR("No transfer queue available", NV_OF_ERR_UNSUPPORTED_FEATURE);
    }

    vkGetDeviceQueue(m_device, m_queueFamilyIndex, 0, &m_queue);
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memoryProperties);

    VkCommandPoolCreateInfo cmdPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmdPoolCreateInfo.queueFamilyIndex = m_queueFamilyIndex;
    VK_API_CALL(vkCreateCommandPool(m_device, &cmdPoolCreateInfo, NULL, &m_cmdPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = MAX_COMMAND_BUFFFERS;
    cmdAllocInfo.commandPool = m_cmdPool;
    VK_API_CALL(vkAllocateCommandBuffers(m_device, &cmdAllocInfo, m_cmdBuffer));

    VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < MAX_COMMAND_BUFFFERS; i++)
    {
        VK_API_CALL(vkCreateFence(m_device, &fenceCreateInfo, NULL, &m_fence[i]));
    }
}

NvOFVkAPI::~NvOFVkAPI()
{
    for (uint32_t i = 0; i < MAX_COMMAND_BUFFFERS; i++)
    {
        if (m_fence[i])
        {
            vkDestroyFence(m_device, m_fence[i], NULL);
            m_fence[i] = VK_NULL_HANDLE;
        }
    }
    if (m_cmdBuffer[0])
    {
        vkFreeCommandBuffers(m_device, m_cmdPool, MAX_COMMAND_BUFFFERS, m_cmdBuffer);
        m_cmdBuffer[0] = VK_NULL_HANDLE;
    }
    if (m_cmdPool)
    {
        vkDestroyCommandPool(m_device, m_cmdPool, NULL);
        m_cmdPool = VK_NULL_HANDLE;
    }

    if (m_ofAPI)
    {
        m_ofAPI->nvOFDestroy(m_hOF);
    }
}

VkCommandBuffer
NvOFVkAPI::GetCommandBuffer()
{
    VK_API_CALL(vkWaitForFences(m_device, 1, &m_fence[m_cmdBufferIndex], VK_TRUE, UINT64_MAX));
    VK_API_CALL(vkResetFences(m_device, 1, &m_fence[m_cmdBufferIndex]));
    VK_API_CALL(vkResetCommandBuffer(m_cmdBuffer[m_cmdBufferIndex], 0));

    VkCommandBufferBeginInfo cmdBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VK_API_CALL(vkBeginCommandBuffer(m_cmdBuffer[m_cmdBufferIndex], &cmdBeginInfo));
    return m_cmdBuffer[m_cmdBufferIndex];
}

void
NvOFVkAPI::SubmitCommandBuffer(VkSubmitInfo* submitInfo, bool waitForQueue)
{
    VK_API_CALL(vkEndCommandBuffer(m_cmdBuffer[m_cmdBufferIndex]));

    submitInfo->commandBufferCount = 1;
    submitInfo->pCommandBuffers = &m_cmdBuffer[m_cmdBufferIndex];
    VK_API_CALL(vkQueueSubmit(m_queue, 1, submitInfo, m_fence[m_cmdBufferIndex]));

    m_cmdBufferIndex = (m_cmdBufferIndex + 1) % MAX_COMMAND_BUFFFERS;
    if (waitForQueue)
    {
        VK_API_CALL(vkQueueWaitIdle(m_queue));
    }
}

void
NvOFVkAPI::SubmitCommandBuffer(VkSubmitInfo2* submitInfo, bool waitForQueue)
{
    VK_API_CALL(vkEndCommandBuffer(m_cmdBuffer[m_cmdBufferIndex]));

    VkCommandBufferSubmitInfo commandBufferSubmitInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    commandBufferSubmitInfo.commandBuffer = m_cmdBuffer[m_cmdBufferIndex];
    commandBufferSubmitInfo.deviceMask = 0;

    submitInfo->commandBufferInfoCount = 1;
    submitInfo->pCommandBufferInfos = &commandBufferSubmitInfo;
    VK_API_CALL(vkQueueSubmit2(m_queue, 1, submitInfo, m_fence[m_cmdBufferIndex]));

    m_cmdBufferIndex = (m_cmdBufferIndex + 1) % MAX_COMMAND_BUFFFERS;
    if (waitForQueue)
    {
        VK_API_CALL(vkQueueWaitIdle(m_queue));
    }
}

NvOFObj NvOFVk::Create(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, 
                       uint32_t nWidth, uint32_t nHeight, NV_OF_BUFFER_FORMAT eInBufFmt, 
                       NV_OF_MODE eMode, NV_OF_PERF_LEVEL preset)
{
    std::unique_ptr<NvOF> ofObj(new NvOFVk(instance, physicalDevice, device, nWidth, nHeight, eInBufFmt, eMode, preset));
    return ofObj;
}

NvOFVk::NvOFVk(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, 
               uint32_t nWidth, uint32_t nHeight, NV_OF_BUFFER_FORMAT eInBufFmt, 
               NV_OF_MODE eMode, NV_OF_PERF_LEVEL preset) : 
    NvOF(nWidth, nHeight, eInBufFmt, eMode, preset),
    m_vkDevice(VK_NULL_HANDLE)
{
    m_NvOFAPI = std::make_shared<NvOFVkAPI>(instance, physicalDevice, device);
}

void NvOFVk::DoGetOutputGridSizes(uint32_t* vals, uint32_t* size)
{
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFGetCaps(m_NvOFAPI->GetHandle(), NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES, vals, size));
}

void NvOFVk::DoGetROISupport(uint32_t* vals, uint32_t* size)
{
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFGetCaps(m_NvOFAPI->GetHandle(), NV_OF_CAPS_SUPPORT_ROI, vals, size));
}

void NvOFVk::DoExecute(const NV_OF_EXECUTE_INPUT_PARAMS& executeInParams,  NV_OF_EXECUTE_OUTPUT_PARAMS& executeOutParams, void* arrWaitSyncs, uint32_t numWaitSyncs, void* pSignalSync)
{
    NV_OF_EXECUTE_INPUT_PARAMS_VK executeInParamsVk;
    memcpy(&executeInParamsVk, &executeInParams, sizeof(executeInParams));

    if (arrWaitSyncs == nullptr)
    {
        NVOF_THROW_ERROR("arrWaitSyncs must be set to an array of _NV_OF_SYNC_VK. Execute() will wait for these semaphores to reach before execution", NV_OF_ERR_INVALID_PARAM);
    }
    if (numWaitSyncs == 0)
    {
        NVOF_THROW_ERROR("numWaitSyncs must be non-zero", NV_OF_ERR_INVALID_PARAM);
    }
    if (pSignalSync == nullptr)
    {
        NVOF_THROW_ERROR("pSignalSync must be set to a _NV_OF_SYNC_VK pointer", NV_OF_ERR_INVALID_PARAM);
    }

    executeInParamsVk.pWaitSyncs = (_NV_OF_SYNC_VK*) arrWaitSyncs;
    executeInParamsVk.numWaitSyncs = numWaitSyncs;

    NV_OF_EXECUTE_OUTPUT_PARAMS_VK executeOutParamsVk;
    memcpy(&executeOutParamsVk, &executeOutParams, sizeof(executeOutParams));
    executeOutParamsVk.pSignalSync = (_NV_OF_SYNC_VK*)pSignalSync;

    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFExecuteVk(m_NvOFAPI->GetHandle(), &executeInParamsVk, &executeOutParamsVk));
}

void NvOFVk::DoInit(const NV_OF_INIT_PARAMS& initParams)
{
    NVOF_API_CALL(m_NvOFAPI->GetAPI()->nvOFInit(m_NvOFAPI->GetHandle(), &initParams));
}

std::vector<NvOFBufferObj>
NvOFVk::DoAllocBuffers(NV_OF_BUFFER_DESCRIPTOR ofBufferDesc,
    uint32_t elementSize, uint32_t numBuffers, void* arrOutputFencePoint, uint32_t numOutputFencePoint)
{
    std::vector<std::unique_ptr<NvOFBuffer>> ofBuffers;
    return ofBuffers;
}

NvOFBufferObj NvOFVk::DoRegisterBuffers(NV_OF_BUFFER_DESCRIPTOR ofBufferDesc,
    uint32_t elementSize, const void* pResource, void* waitSync, void* signalSync)
{
    const NV_OF_RESOURCE_VK* pVkResource = reinterpret_cast<const NV_OF_RESOURCE_VK*>(pResource);

    if (!pVkResource || !pVkResource->image || !pVkResource->memory || !pVkResource->allocationSize)
    {
        NVOF_THROW_ERROR("Invalid resource pointer", NV_OF_ERR_INVALID_PARAM);
    }

    NvOFBufferObj ofBuffer(new NvOFBufferVk<RWPolicyDeviceAndHost>(m_NvOFAPI, ofBufferDesc, elementSize,
        pVkResource));
    return ofBuffer;
}

template<typename RWPolicy>
NvOFBufferVk<RWPolicy>::NvOFBufferVk(std::shared_ptr<NvOFVkAPI> ofVk, const NV_OF_BUFFER_DESCRIPTOR& nvBufDesc,
    uint32_t elementSize, const NV_OF_RESOURCE_VK* pResource) :
    NvOFBuffer(nvBufDesc, elementSize), RWPolicy(ofVk.get(), nvBufDesc, pResource), m_nvOFVk(ofVk)
{
    NV_OF_REGISTER_RESOURCE_PARAMS_VK registerParams{};
    registerParams.image = pResource->image;
    registerParams.format = NvOFBufferFormatToVkFormat(nvBufDesc.bufferFormat);
    registerParams.hOFGpuBuffer = &m_hGPUBuffer;
    NVOF_API_CALL(ofVk->GetAPI()->nvOFRegisterResourceVk(ofVk->GetHandle(), &registerParams));
    m_format = NvOFBufferFormatToVkFormat(nvBufDesc.bufferFormat);
    m_resource = *pResource;
}

template<typename RWPolicy>
NvOFBufferVk<RWPolicy>::~NvOFBufferVk()
{
    NV_OF_UNREGISTER_RESOURCE_PARAMS_VK param;
    param.hOFGpuBuffer = getOFBufferHandle();
    m_nvOFVk->GetAPI()->nvOFUnregisterResourceVk(&param);
}

template<typename RWPolicy>
void NvOFBufferVk<RWPolicy>::UploadData(const void* pData, void* pWaitSync, void* pSignalSync)
{
    RWPolicy::UploadData(pData, pWaitSync, pSignalSync);
}

template<typename RWPolicy>
void NvOFBufferVk<RWPolicy>::DownloadData(void* data, void* pInputFencePoint)
{
    RWPolicy::DownloadData(data, pInputFencePoint);
}

RWPolicyDeviceAndHost::RWPolicyDeviceAndHost(NvOFVkAPI* nvof, NV_OF_BUFFER_DESCRIPTOR nvofBufDesc, const NV_OF_RESOURCE_VK* pGPUResource):
    m_nvOFVk(nvof),
    m_resource(*pGPUResource),
    m_desc(nvofBufDesc),
    m_totalBytes(0),
    m_stagingBuffer(VK_NULL_HANDLE),
    m_stagingBufferMem(VK_NULL_HANDLE)
{
    VkFormat format = NvOFBufferFormatToVkFormat(nvofBufDesc.bufferFormat);
    m_numSubResources = GetNumberOfPlanes(format);
    VkImageSubresource subresource = {};

    VkImageCreateInfo imageCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = format;
    imageCreateInfo.extent = { nvofBufDesc.width, nvofBufDesc.height, 1 };
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
    VkImage image;
    VK_API_CALL(vkCreateImage(m_nvOFVk->GetDevice(), &imageCreateInfo, NULL, &image));

    for (uint32_t plane = 0; plane < m_numSubResources; plane++) {
        subresource.aspectMask = m_numSubResources == 1 ? VK_IMAGE_ASPECT_COLOR_BIT : (VK_IMAGE_ASPECT_PLANE_0_BIT << plane);
        vkGetImageSubresourceLayout(m_nvOFVk->GetDevice(), image, &subresource, &m_layout[plane]);
        m_totalBytes += m_layout[plane].size; 
    }

    vkDestroyImage(m_nvOFVk->GetDevice(), image, NULL);

    AllocateStagingBuffer(nvofBufDesc, pGPUResource);
}

RWPolicyDeviceAndHost::~RWPolicyDeviceAndHost()
{
    if (m_nvOFVk && m_nvOFVk->GetDevice()) {
        VkDevice device = m_nvOFVk->GetDevice();
        if (m_stagingBuffer) {
            vkDestroyBuffer(device, m_stagingBuffer, NULL);
            m_stagingBuffer = VK_NULL_HANDLE;
        }
        if (m_stagingBufferMem) {
            vkFreeMemory(device, m_stagingBufferMem, NULL);
            m_stagingBufferMem = VK_NULL_HANDLE;
        }
    }
}

void RWPolicyDeviceAndHost::AllocateStagingBuffer(NV_OF_BUFFER_DESCRIPTOR ofBufDesc, const NV_OF_RESOURCE_VK* pGPUResource)
{
    VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferCreateInfo.size = m_totalBytes;
    VK_API_CALL(vkCreateBuffer(m_nvOFVk->GetDevice(), &bufferCreateInfo, NULL, &m_stagingBuffer));

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(m_nvOFVk->GetDevice(), m_stagingBuffer, &mem_reqs);

    VkMemoryAllocateInfo memAllocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    const VkPhysicalDeviceMemoryProperties* pMemoryProperties = m_nvOFVk->GetPhysicalDeviceMemoryProperties();
    memAllocInfo.allocationSize = mem_reqs.size;
    for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++) {
        if (mem_reqs.memoryTypeBits & (1 << i)) {
            if (pMemoryProperties->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
                memAllocInfo.memoryTypeIndex = i;
                break;
            }
        }
    }

    VK_API_CALL(vkAllocateMemory(m_nvOFVk->GetDevice(), &memAllocInfo, NULL, &m_stagingBufferMem));
    VK_API_CALL(vkBindBufferMemory(m_nvOFVk->GetDevice(), m_stagingBuffer, m_stagingBufferMem, 0));
}

void RWPolicyDeviceAndHost::UploadData(const void* pSysMem, void* pWaitSync, void* pSignalSync)
{
    VkDevice device = m_nvOFVk->GetDevice();
    auto waitSync = (NV_OF_SYNC_VK*)pWaitSync;
    auto signalSync = (NV_OF_SYNC_VK*)pSignalSync;

    void* pStagingBufferData;
    VK_API_CALL(vkMapMemory(device, m_stagingBufferMem, 0, VK_WHOLE_SIZE, 0, &pStagingBufferData));

    switch (m_desc.bufferFormat)
    {
    case NV_OF_BUFFER_FORMAT_ABGR8:
        memcpy(pStagingBufferData, pSysMem, m_desc.width * m_desc.height * 4);
        break;
    case NV_OF_BUFFER_FORMAT_GRAYSCALE8:
        memcpy(pStagingBufferData, pSysMem, m_desc.width * m_desc.height);
        break;
    case NV_OF_BUFFER_FORMAT_NV12:
        memcpy(pStagingBufferData, pSysMem, m_desc.width * m_desc.height);
        memcpy((uint8_t*)pStagingBufferData + m_layout[0].size, (uint8_t*)pSysMem + m_desc.width * m_desc.height, (m_desc.width / 2) * (m_desc.height / 2) * 2);
        break;
    default:
        ;
    }

    vkUnmapMemory(device, m_stagingBufferMem);

    VkCommandBuffer cmd = m_nvOFVk->GetCommandBuffer();

    VkBufferImageCopy bufferImageCopy = {};
    bufferImageCopy.bufferRowLength = m_desc.width;
    bufferImageCopy.imageExtent.width = m_desc.width;
    bufferImageCopy.imageExtent.height = m_desc.height;
    bufferImageCopy.imageExtent.depth = 1;
    bufferImageCopy.imageSubresource.aspectMask = m_numSubResources == 1 ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_PLANE_0_BIT;
    bufferImageCopy.imageSubresource.layerCount = 1;
    vkCmdCopyBufferToImage(cmd, m_stagingBuffer, m_resource.image, VK_IMAGE_LAYOUT_GENERAL, 1, &bufferImageCopy);

    if (m_numSubResources > 1) {
        bufferImageCopy.bufferOffset += m_layout[0].size;

        bufferImageCopy.bufferRowLength = (m_desc.width + 1) / 2;
        bufferImageCopy.imageExtent.width = (m_desc.width + 1) / 2;
        bufferImageCopy.imageExtent.height = (m_desc.height + 1) / 2;
        bufferImageCopy.imageExtent.depth = 1;
        bufferImageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
        bufferImageCopy.imageSubresource.layerCount = 1;
        vkCmdCopyBufferToImage(cmd, m_stagingBuffer, m_resource.image, VK_IMAGE_LAYOUT_GENERAL, 1, &bufferImageCopy);
    }

    VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkTimelineSemaphoreSubmitInfo timelineSemaphoreSubmitInfo = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    if (waitSync && waitSync->value) {
        submitInfo.pNext = &timelineSemaphoreSubmitInfo;
        timelineSemaphoreSubmitInfo.waitSemaphoreValueCount = 1;
        timelineSemaphoreSubmitInfo.pWaitSemaphoreValues = &waitSync->value;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &waitSync->semaphore;
        submitInfo.pWaitDstStageMask = &waitDstStageMask;
    }
    if (signalSync && signalSync->value) {
        submitInfo.pNext = &timelineSemaphoreSubmitInfo;
        timelineSemaphoreSubmitInfo.signalSemaphoreValueCount = 1;
        timelineSemaphoreSubmitInfo.pSignalSemaphoreValues = &signalSync->value;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &signalSync->semaphore;
    }
    m_nvOFVk->SubmitCommandBuffer(&submitInfo);
}

void RWPolicyDeviceAndHost::DownloadData(void* pSysMem, void* pWaitSync)
{
    VkDevice device = m_nvOFVk->GetDevice();
    auto waitSync = (NV_OF_SYNC_VK*)pWaitSync;

    VkCommandBuffer cmd = m_nvOFVk->GetCommandBuffer();

    VkBufferImageCopy bufferImageCopy = {};
    bufferImageCopy.bufferRowLength = m_desc.width;
    bufferImageCopy.imageExtent.width = m_desc.width;
    bufferImageCopy.imageExtent.height = m_desc.height;
    bufferImageCopy.imageExtent.depth = 1;
    bufferImageCopy.imageSubresource.aspectMask = m_numSubResources == 1 ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_PLANE_0_BIT;
    bufferImageCopy.imageSubresource.layerCount = 1;
    vkCmdCopyImageToBuffer(cmd, m_resource.image, VK_IMAGE_LAYOUT_GENERAL, m_stagingBuffer, 1, &bufferImageCopy);

    if (m_numSubResources > 1) {
        bufferImageCopy.bufferOffset += m_layout[0].size;

        bufferImageCopy.bufferRowLength = (m_desc.width + 1) / 2;
        bufferImageCopy.imageExtent.width = (m_desc.width + 1) / 2;
        bufferImageCopy.imageExtent.height = (m_desc.height + 1) / 2;
        bufferImageCopy.imageExtent.depth = 1;
        bufferImageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
        bufferImageCopy.imageSubresource.layerCount = 1;
        vkCmdCopyImageToBuffer(cmd, m_resource.image, VK_IMAGE_LAYOUT_GENERAL, m_stagingBuffer, 1, &bufferImageCopy);
    }

    VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkTimelineSemaphoreSubmitInfo timelineSemaphoreSubmitInfo = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    if (waitSync && waitSync->value) {
        submitInfo.pNext = &timelineSemaphoreSubmitInfo;
        timelineSemaphoreSubmitInfo.waitSemaphoreValueCount = 1;
        timelineSemaphoreSubmitInfo.pWaitSemaphoreValues = &waitSync->value;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &waitSync->semaphore;
        submitInfo.pWaitDstStageMask = &waitDstStageMask;
    }
    m_nvOFVk->SubmitCommandBuffer(&submitInfo, true);

    void* pStagingBufferData;
    VK_API_CALL(vkMapMemory(device, m_stagingBufferMem, 0, VK_WHOLE_SIZE, 0, &pStagingBufferData));

    switch (m_desc.bufferFormat)
    {
    case NV_OF_BUFFER_FORMAT_SHORT2:
    case NV_OF_BUFFER_FORMAT_UINT:
        memcpy(pSysMem, pStagingBufferData, m_desc.width * m_desc.height * 4);
        break;
    case NV_OF_BUFFER_FORMAT_UINT8:
        memcpy(pSysMem, pStagingBufferData, m_desc.width * m_desc.height);
        break;
    default:
        ;
    }
    vkUnmapMemory(device, m_stagingBufferMem);
}

NvOFSyncVk::NvOFSyncVk(VkDevice device, uint64_t startValue) :
    NvOFSync(), 
    m_device(device)
{
    m_syncObject.reset(new NV_OF_SYNC_VK());
    memset(m_syncObject.get(), 0, sizeof(NV_OF_TIMELINE_SEMAPHORE));

    NV_OF_TIMELINE_SEMAPHORE* pTimelineSemaphore = (NV_OF_TIMELINE_SEMAPHORE*)m_syncObject.get();

    VkSemaphoreTypeCreateInfo semTypeInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    semTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    semTypeInfo.initialValue = startValue;

    VkSemaphoreCreateInfo semCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &semTypeInfo };
    VK_API_CALL(vkCreateSemaphore(device, &semCreateInfo, NULL, &pTimelineSemaphore->semaphore));
    m_syncObject->value = startValue;
}

NvOFSyncVk::~NvOFSyncVk()
{
    if (m_syncObject) 
    {
        NV_OF_TIMELINE_SEMAPHORE* pTimelineSemaphore = (NV_OF_TIMELINE_SEMAPHORE*)m_syncObject.get();
        if (pTimelineSemaphore->semaphore)
        {
            vkDestroySemaphore(m_device, pTimelineSemaphore->semaphore, NULL);
            pTimelineSemaphore->semaphore = VK_NULL_HANDLE;
        }
    }
}

