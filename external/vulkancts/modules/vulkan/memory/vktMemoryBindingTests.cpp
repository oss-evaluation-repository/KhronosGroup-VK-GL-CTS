/*-------------------------------------------------------------------------
 * Vulkan Conformance Tests
 * ------------------------
 *
 * Copyright (c) 2017 The Khronos Group Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *//*!
 * \file
 * \brief Memory binding test excercising VK_KHR_bind_memory2 extension.
 *//*--------------------------------------------------------------------*/

#include "vktMemoryBindingTests.hpp"

#include "vktTestCase.hpp"
#include "tcuTestLog.hpp"
#include "vktCustomInstancesDevices.hpp"

#include "vkPlatform.hpp"
#include "tcuCommandLine.hpp"
#include "gluVarType.hpp"
#include "deStringUtil.hpp"
#include "vkPrograms.hpp"
#include "vkQueryUtil.hpp"
#include "vkRefUtil.hpp"
#include "deSharedPtr.hpp"
#include "vktTestCase.hpp"
#include "vkTypeUtil.hpp"
#include "vkCmdUtil.hpp"
#include "vkImageUtil.hpp"

#include <algorithm>

namespace vkt
{
namespace memory
{
namespace
{

using namespace vk;

typedef const VkMemoryDedicatedAllocateInfo ConstDedicatedInfo;
typedef de::SharedPtr<Move<VkDeviceMemory>> MemoryRegionPtr;
typedef std::vector<MemoryRegionPtr> MemoryRegionsList;
typedef de::SharedPtr<Move<VkBuffer>> BufferPtr;
typedef std::vector<BufferPtr> BuffersList;
typedef de::SharedPtr<Move<VkImage>> ImagePtr;
typedef std::vector<ImagePtr> ImagesList;
typedef std::vector<VkBindBufferMemoryInfo> BindBufferMemoryInfosList;
typedef std::vector<VkBindImageMemoryInfo> BindImageMemoryInfosList;
#ifndef CTS_USES_VULKANSC
typedef std::vector<VkBindMemoryStatusKHR> BindMemoryStatusList;
#endif // CTS_USES_VULKANSC

class MemoryMappingRAII
{
public:
    MemoryMappingRAII(const DeviceInterface &deviceInterface, const VkDevice &device, VkDeviceMemory deviceMemory,
                      VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags)
        : vk(deviceInterface)
        , dev(device)
        , memory(deviceMemory)
        , hostPtr(DE_NULL)

    {
        vk.mapMemory(dev, memory, offset, size, flags, &hostPtr);
    }

    ~MemoryMappingRAII()
    {
        vk.unmapMemory(dev, memory);
        hostPtr = DE_NULL;
    }

    void *ptr()
    {
        return hostPtr;
    }

    void flush()
    {
        const VkMappedMemoryRange range = makeMemoryRange(0, VK_WHOLE_SIZE);
        VK_CHECK(vk.flushMappedMemoryRanges(dev, 1u, &range));
    }

    void invalidate()
    {
        const VkMappedMemoryRange range = makeMemoryRange(0, VK_WHOLE_SIZE);
        VK_CHECK(vk.invalidateMappedMemoryRanges(dev, 1u, &range));
    }

protected:
    const DeviceInterface &vk;
    const VkDevice &dev;
    VkDeviceMemory memory;
    void *hostPtr;

    const VkMappedMemoryRange makeMemoryRange(VkDeviceSize offset, VkDeviceSize size)
    {
        const VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, DE_NULL, memory, offset, size};
        return range;
    }
};

class SimpleRandomGenerator
{
public:
    SimpleRandomGenerator(uint32_t seed) : value(seed)
    {
    }
    uint32_t getNext()
    {
        value += 1;
        value ^= (value << 21);
        value ^= (value >> 15);
        value ^= (value << 4);
        return value;
    }

protected:
    uint32_t value;
};

enum PriorityMode
{
    PRIORITY_MODE_DEFAULT,
    PRIORITY_MODE_STATIC,
    PRIORITY_MODE_DYNAMIC,
};

struct BindingCaseParameters
{
    VkBufferCreateFlags flags;
    VkBufferUsageFlags usage;
    VkSharingMode sharing;
    VkDeviceSize bufferSize;
    VkExtent3D imageSize;
    uint32_t targetsCount;
    VkImageCreateFlags imageCreateFlags;
    PriorityMode priorityMode;
    bool checkIndividualResult;
};

BindingCaseParameters makeBindingCaseParameters(uint32_t targetsCount, uint32_t width, uint32_t height,
                                                VkImageCreateFlags imageCreateFlags, PriorityMode priorityMode,
                                                bool checkIndividualResult)
{
    BindingCaseParameters params;
    deMemset(&params, 0, sizeof(BindingCaseParameters));
    params.imageSize.width  = width;
    params.imageSize.height = height;
    params.imageSize.depth  = 1;
    params.bufferSize   = params.imageSize.width * params.imageSize.height * params.imageSize.depth * sizeof(uint32_t);
    params.usage        = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    params.targetsCount = targetsCount;
    params.imageCreateFlags      = imageCreateFlags;
    params.priorityMode          = priorityMode;
    params.checkIndividualResult = checkIndividualResult;
    return params;
}

BindingCaseParameters makeBindingCaseParameters(uint32_t targetsCount, VkBufferUsageFlags usage, VkSharingMode sharing,
                                                VkDeviceSize bufferSize, VkImageCreateFlags imageCreateFlags,
                                                PriorityMode priorityMode, bool checkIndividualResult)
{
    BindingCaseParameters params = {
        0,                    // VkBufferCreateFlags flags;
        usage,                // VkBufferUsageFlags usage;
        sharing,              // VkSharingMode sharing;
        bufferSize,           // VkDeviceSize bufferSize;
        {0u, 0u, 0u},         // VkExtent3D imageSize;
        targetsCount,         // uint32_t targetsCount;
        imageCreateFlags,     // VkImageCreateFlags    imageCreateFlags
        priorityMode,         // PriorityMode            priorityMode
        checkIndividualResult // bool                checkIndividualResult
    };
    return params;
}

VkImageCreateInfo makeImageCreateInfo(BindingCaseParameters &params)
{
    const VkImageCreateInfo imageParams = {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,                               // VkStructureType sType;
        DE_NULL,                                                           // const void* pNext;
        params.imageCreateFlags,                                           // VkImageCreateFlags flags;
        VK_IMAGE_TYPE_2D,                                                  // VkImageType imageType;
        VK_FORMAT_R8G8B8A8_UINT,                                           // VkFormat format;
        params.imageSize,                                                  // VkExtent3D extent;
        1u,                                                                // uint32_t mipLevels;
        1u,                                                                // uint32_t arrayLayers;
        VK_SAMPLE_COUNT_1_BIT,                                             // VkSampleCountFlagBits samples;
        VK_IMAGE_TILING_LINEAR,                                            // VkImageTiling tiling;
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // VkImageUsageFlags usage;
        VK_SHARING_MODE_EXCLUSIVE,                                         // VkSharingMode sharingMode;
        0u,                                                                // uint32_t queueFamilyIndexCount;
        DE_NULL,                                                           // const uint32_t* pQueueFamilyIndices;
        VK_IMAGE_LAYOUT_UNDEFINED,                                         // VkImageLayout initialLayout;
    };
    return imageParams;
}

VkBufferCreateInfo makeBufferCreateInfo(Context &ctx, BindingCaseParameters &params)
{
    const uint32_t queueFamilyIndex = ctx.getUniversalQueueFamilyIndex();
    VkBufferCreateInfo bufferParams = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // VkStructureType sType;
        DE_NULL,                              // const void* pNext;
        params.flags,                         // VkBufferCreateFlags flags;
        params.bufferSize,                    // VkDeviceSize size;
        params.usage,                         // VkBufferUsageFlags usage;
        params.sharing,                       // VkSharingMode sharingMode;
        1u,                                   // uint32_t queueFamilyIndexCount;
        &queueFamilyIndex,                    // const uint32_t* pQueueFamilyIndices;
    };
    return bufferParams;
}

const VkMemoryAllocateInfo makeMemoryAllocateInfo(VkMemoryRequirements &memReqs, const void *next)
{
    const uint32_t heapTypeIndex              = (uint32_t)deCtz32(memReqs.memoryTypeBits);
    const VkMemoryAllocateInfo allocateParams = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, // VkStructureType sType;
        next,                                   // const void* pNext;
        memReqs.size,                           // VkDeviceSize allocationSize;
        heapTypeIndex,                          // uint32_t memoryTypeIndex;
    };
    return allocateParams;
}

enum MemoryHostVisibility
{
    MemoryAny,
    MemoryHostVisible
};

uint32_t selectMatchingMemoryType(Context &ctx, VkMemoryRequirements &memReqs, MemoryHostVisibility memoryVisibility)
{
    const VkPhysicalDevice vkPhysicalDevice = ctx.getPhysicalDevice();
    const InstanceInterface &vkInstance     = ctx.getInstanceInterface();
    VkPhysicalDeviceMemoryProperties memoryProperties;

    vkInstance.getPhysicalDeviceMemoryProperties(vkPhysicalDevice, &memoryProperties);
    if (memoryVisibility == MemoryHostVisible)
    {
        for (uint32_t typeNdx = 0; typeNdx < memoryProperties.memoryTypeCount; ++typeNdx)
        {
            const bool isInAllowed = (memReqs.memoryTypeBits & (1u << typeNdx)) != 0u;
            const bool hasRightProperties =
                (memoryProperties.memoryTypes[typeNdx].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u;
            if (isInAllowed && hasRightProperties)
                return typeNdx;
        }
    }
    return (uint32_t)deCtz32(memReqs.memoryTypeBits);
}

const VkMemoryAllocateInfo makeMemoryAllocateInfo(Context &ctx, VkMemoryRequirements &memReqs,
                                                  MemoryHostVisibility memoryVisibility)
{
    const uint32_t heapTypeIndex              = selectMatchingMemoryType(ctx, memReqs, memoryVisibility);
    const VkMemoryAllocateInfo allocateParams = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, // VkStructureType sType;
        DE_NULL,                                // const void* pNext;
        memReqs.size,                           // VkDeviceSize allocationSize;
        heapTypeIndex,                          // uint32_t memoryTypeIndex;
    };
    return allocateParams;
}

ConstDedicatedInfo makeDedicatedAllocationInfo(VkBuffer buffer)
{
    ConstDedicatedInfo dedicatedAllocationInfo = {
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, // VkStructureType        sType
        DE_NULL,                                          // const void*            pNext
        DE_NULL,                                          // VkImage                image
        buffer                                            // VkBuffer                buffer
    };
    return dedicatedAllocationInfo;
}

ConstDedicatedInfo makeDedicatedAllocationInfo(VkImage image)
{
    ConstDedicatedInfo dedicatedAllocationInfo = {
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, // VkStructureType        sType
        DE_NULL,                                          // const void*            pNext
        image,                                            // VkImage                image
        DE_NULL                                           // VkBuffer                buffer
    };
    return dedicatedAllocationInfo;
}

const VkBindBufferMemoryInfo makeBufferMemoryBindingInfo(VkBuffer buffer, VkDeviceMemory memory, const void *pNext)
{
    const VkBindBufferMemoryInfo bufferMemoryBinding = {
        VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO, // VkStructureType sType;
        pNext,                                     // const void* pNext;
        buffer,                                    // VkBuffer buffer;
        memory,                                    // VkDeviceMemory memory;
        0u,                                        // VkDeviceSize memoryOffset;
    };
    return bufferMemoryBinding;
}

const VkBindImageMemoryInfo makeImageMemoryBindingInfo(VkImage image, VkDeviceMemory memory, const void *pNext)
{
    const VkBindImageMemoryInfo imageMemoryBinding = {
        VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO, // VkStructureType sType;
        pNext,                                    // const void* pNext;
        image,                                    // VkImage image;
        memory,                                   // VkDeviceMemory memory;
        0u,                                       // VkDeviceSize memoryOffset;
    };
    return imageMemoryBinding;
}

#ifndef CTS_USES_VULKANSC
const VkMemoryPriorityAllocateInfoEXT makeMemoryPriorityAllocateInfo(const void *pNext, float priority)
{
    const VkMemoryPriorityAllocateInfoEXT info = {
        VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT, // VkStructureType sType;
        pNext,                                               // const void* pNext;
        priority,                                            // float                priority
    };
    return info;
}

const VkBindMemoryStatusKHR makeBindMemoryStatus(VkResult *pResult)
{
    const VkBindMemoryStatusKHR bindMemoryStatus = {
        VK_STRUCTURE_TYPE_BIND_MEMORY_STATUS_KHR, // VkStructureType sType;
        DE_NULL,                                  // const void* pNext;
        pResult,                                  // VkResult* pResult;
    };
    return bindMemoryStatus;
}
#endif

enum TransferDirection
{
    TransferToResource   = 0,
    TransferFromResource = 1
};

const VkBufferMemoryBarrier makeMemoryBarrierInfo(VkBuffer buffer, VkDeviceSize size, TransferDirection direction)
{
    const bool fromRes = direction == TransferFromResource;
    const VkAccessFlags srcMask =
        static_cast<VkAccessFlags>(fromRes ? VK_ACCESS_HOST_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT);
    const VkAccessFlags dstMask =
        static_cast<VkAccessFlags>(fromRes ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_HOST_READ_BIT);
    const VkBufferMemoryBarrier bufferBarrier = {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, // VkStructureType sType;
        DE_NULL,                                 // const void* pNext;
        srcMask,                                 // VkAccessFlags srcAccessMask;
        dstMask,                                 // VkAccessFlags dstAccessMask;
        VK_QUEUE_FAMILY_IGNORED,                 // uint32_t srcQueueFamilyIndex;
        VK_QUEUE_FAMILY_IGNORED,                 // uint32_t dstQueueFamilyIndex;
        buffer,                                  // VkBuffer buffer;
        0u,                                      // VkDeviceSize offset;
        size                                     // VkDeviceSize size;
    };
    return bufferBarrier;
}

const VkImageMemoryBarrier makeMemoryBarrierInfo(VkImage image, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                                 VkImageLayout oldLayout, VkImageLayout newLayout)
{
    const VkImageAspectFlags aspect         = VK_IMAGE_ASPECT_COLOR_BIT;
    const VkImageMemoryBarrier imageBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, // VkStructureType sType;
                                               DE_NULL,                                // const void* pNext;
                                               srcAccess,                              // VkAccessFlags srcAccessMask;
                                               dstAccess,                              // VkAccessFlags dstAccessMask;
                                               oldLayout,                              // VkImageLayout oldLayout;
                                               newLayout,                              // VkImageLayout newLayout;
                                               VK_QUEUE_FAMILY_IGNORED,                // uint32_t srcQueueFamilyIndex;
                                               VK_QUEUE_FAMILY_IGNORED,                // uint32_t dstQueueFamilyIndex;
                                               image,                                  // VkImage image;
                                               {
                                                   // VkImageSubresourceRange subresourceRange;
                                                   aspect, // VkImageAspectFlags aspect;
                                                   0u,     // uint32_t baseMipLevel;
                                                   1u,     // uint32_t mipLevels;
                                                   0u,     // uint32_t baseArraySlice;
                                                   1u,     // uint32_t arraySize;
                                               }};
    return imageBarrier;
}

Move<VkCommandBuffer> createCommandBuffer(const DeviceInterface &vk, VkDevice device, VkCommandPool commandPool)
{
    const VkCommandBufferAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, DE_NULL, commandPool,
                                                   VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
    return allocateCommandBuffer(vk, device, &allocInfo);
}

class BaseTestInstance : public TestInstance
{
public:
    BaseTestInstance(Context &ctx, BindingCaseParameters params) : TestInstance(ctx), m_params(params)
    {
#ifndef CTS_USES_VULKANSC
        if (m_params.priorityMode == PRIORITY_MODE_DYNAMIC)
        {
            VkInstance instance(m_context.getInstance());
            InstanceDriver instanceDriver(m_context.getPlatformInterface(), instance);
            const float queuePriority = 1.0f;

            VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT pageableDeviceLocalMemoryFeature = {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT, // VkStructureType                    sType
                DE_NULL,  // const void*                        pNext
                VK_FALSE, // VkBool32 pageableDeviceLocalMemory;
            };

            VkPhysicalDeviceMaintenance6FeaturesKHR maintenance6Feature = {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES_KHR, // VkStructureType                    sType
                DE_NULL,  // const void*                        pNext
                VK_FALSE, // VkBool32 maintenance6;
            };
            if (m_params.checkIndividualResult)
                pageableDeviceLocalMemoryFeature.pNext = &maintenance6Feature;

            VkPhysicalDeviceFeatures features;
            deMemset(&features, 0, sizeof(vk::VkPhysicalDeviceFeatures));

            VkPhysicalDeviceFeatures2 features2 = {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, // VkStructureType                    sType
                &pageableDeviceLocalMemoryFeature,            // const void*                        pNext
                features                                      // VkPhysicalDeviceFeatures            features
            };

            instanceDriver.getPhysicalDeviceFeatures2(m_context.getPhysicalDevice(), &features2);

            if (!pageableDeviceLocalMemoryFeature.pageableDeviceLocalMemory)
                TCU_FAIL("pageableDeviceLocalMemory feature not supported but VK_EXT_pageable_device_local_memory "
                         "advertised");

            if (m_params.checkIndividualResult && !maintenance6Feature.maintenance6)
                TCU_FAIL("maintenance6 feature not supported but VK_KHR_maintenance6 advertised");

            pageableDeviceLocalMemoryFeature.pageableDeviceLocalMemory = VK_TRUE;

            std::vector<const char *> deviceExtensions;
            deviceExtensions.push_back("VK_EXT_memory_priority");
            deviceExtensions.push_back("VK_EXT_pageable_device_local_memory");
            if (m_params.checkIndividualResult)
                deviceExtensions.push_back("VK_KHR_maintenance6");

            VkDeviceQueueCreateInfo queueInfo = {
                VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, // VkStructureType sType;
                DE_NULL,                                    // const void* pNext;
                0u,                                         // VkDeviceQueueCreateFlags flags;
                0u,                                         // uint32_t queueFamilyIndex;
                1u,                                         // uint32_t queueCount;
                &queuePriority                              // const float* pQueuePriorities;
            };

            const VkDeviceCreateInfo deviceInfo = {
                VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, // VkStructureType sType;
                &features2,                           // const void* pNext;
                (VkDeviceCreateFlags)0,               // VkDeviceCreateFlags flags;
                1u,                                   // uint32_t queueCreateInfoCount;
                &queueInfo,                           // const VkDeviceQueueCreateInfo* pQueueCreateInfos;
                0u,                                   // uint32_t enabledLayerCount;
                DE_NULL,                              // const char* const* ppEnabledLayerNames;
                uint32_t(deviceExtensions.size()),    // uint32_t enabledExtensionCount;
                (deviceExtensions.empty()) ? DE_NULL :
                                             deviceExtensions.data(), // const char* const* ppEnabledExtensionNames;
                DE_NULL // const VkPhysicalDeviceFeatures* pEnabledFeatures;
            };

            m_logicalDevice = createCustomDevice(m_context.getTestContext().getCommandLine().isValidationEnabled(),
                                                 m_context.getPlatformInterface(), instance, instanceDriver,
                                                 m_context.getPhysicalDevice(), &deviceInfo);
        }
#endif // CTS_USES_VULKANSC
        m_logicalDeviceInterface = de::MovePtr<DeviceDriver>(
            new DeviceDriver(m_context.getPlatformInterface(), m_context.getInstance(), getDevice(),
                             m_context.getUsedApiVersion(), m_context.getTestContext().getCommandLine()));
        m_logicalDeviceInterface->getDeviceQueue(getDevice(), m_context.getUniversalQueueFamilyIndex(), 0,
                                                 &m_logicalDeviceQueue);
    };

protected:
    vk::VkDevice getDevice(void)
    {
        return (m_params.priorityMode == PRIORITY_MODE_DYNAMIC) ? m_logicalDevice.get() : m_context.getDevice();
    }
    const DeviceInterface &getDeviceInterface(void)
    {
        return (m_params.priorityMode == PRIORITY_MODE_DYNAMIC) ? *m_logicalDeviceInterface.get() :
                                                                  m_context.getDeviceInterface();
    }
    VkQueue getUniversalQueue(void)
    {
        return (m_params.priorityMode == PRIORITY_MODE_DYNAMIC) ? m_logicalDeviceQueue : m_context.getUniversalQueue();
    }

    template <typename TTarget>
    void createBindingTargets(std::vector<de::SharedPtr<Move<TTarget>>> &targets);

    template <typename TTarget, bool TDedicated>
    void createMemory(std::vector<de::SharedPtr<Move<TTarget>>> &targets, MemoryRegionsList &memory);

    template <typename TTarget>
    void makeBinding(std::vector<de::SharedPtr<Move<TTarget>>> &targets, MemoryRegionsList &memory);

    template <typename TTarget>
    void fillUpResource(Move<VkBuffer> &source, Move<TTarget> &target);

    template <typename TTarget>
    void readUpResource(Move<TTarget> &source, Move<VkBuffer> &target);

    template <typename TTarget>
    void layoutTransitionResource(Move<TTarget> &target);

    void createBuffer(Move<VkBuffer> &buffer, Move<VkDeviceMemory> &memory, VkDeviceSize *memorySize);

    void pushData(VkDeviceMemory memory, uint32_t dataSeed, VkDeviceSize size);

    bool checkData(VkDeviceMemory memory, uint32_t dataSeed, VkDeviceSize size);

    BindingCaseParameters m_params;

private:
    vk::Move<vk::VkDevice> m_logicalDevice;
    de::MovePtr<DeviceDriver> m_logicalDeviceInterface;
    VkQueue m_logicalDeviceQueue;
};

template <>
void BaseTestInstance::createBindingTargets<VkBuffer>(BuffersList &targets)
{
    const uint32_t count      = m_params.targetsCount;
    const VkDevice vkDevice   = getDevice();
    const DeviceInterface &vk = getDeviceInterface();

    targets.reserve(count);
    for (uint32_t i = 0u; i < count; ++i)
    {
        VkBufferCreateInfo bufferParams = makeBufferCreateInfo(m_context, m_params);
        targets.push_back(BufferPtr(new Move<VkBuffer>(vk::createBuffer(vk, vkDevice, &bufferParams))));
    }
}

template <>
void BaseTestInstance::createBindingTargets<VkImage>(ImagesList &targets)
{
    const uint32_t count      = m_params.targetsCount;
    const VkDevice vkDevice   = getDevice();
    const DeviceInterface &vk = getDeviceInterface();

    targets.reserve(count);
    for (uint32_t i = 0u; i < count; ++i)
    {
        VkImageCreateInfo imageParams = makeImageCreateInfo(m_params);
        targets.push_back(ImagePtr(new Move<VkImage>(createImage(vk, vkDevice, &imageParams))));
    }
}

template <>
void BaseTestInstance::createMemory<VkBuffer, false>(BuffersList &targets, MemoryRegionsList &memory)
{
    const uint32_t count      = static_cast<uint32_t>(targets.size());
    const DeviceInterface &vk = getDeviceInterface();
    const VkDevice vkDevice   = getDevice();

    memory.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        VkMemoryRequirements memReqs;

        vk.getBufferMemoryRequirements(vkDevice, **targets[i], &memReqs);

#ifdef CTS_USES_VULKANSC
        const VkMemoryAllocateInfo memAlloc = makeMemoryAllocateInfo(memReqs, DE_NULL);
#else
        VkMemoryPriorityAllocateInfoEXT priority = makeMemoryPriorityAllocateInfo(DE_NULL, ((float)i) / ((float)count));
        const VkMemoryAllocateInfo memAlloc =
            makeMemoryAllocateInfo(memReqs, (m_params.priorityMode == PRIORITY_MODE_STATIC) ? &priority : DE_NULL);
#endif
        VkDeviceMemory rawMemory = DE_NULL;

        vk.allocateMemory(vkDevice, &memAlloc, (VkAllocationCallbacks *)DE_NULL, &rawMemory);

#ifndef CTS_USES_VULKANSC
        if (m_params.priorityMode == PRIORITY_MODE_DYNAMIC)
            vk.setDeviceMemoryPriorityEXT(vkDevice, rawMemory, priority.priority);
#endif // CTS_USES_VULKANSC

        memory.push_back(MemoryRegionPtr(new Move<VkDeviceMemory>(check<VkDeviceMemory>(rawMemory),
                                                                  Deleter<VkDeviceMemory>(vk, vkDevice, DE_NULL))));
    }
}

template <>
void BaseTestInstance::createMemory<VkImage, false>(ImagesList &targets, MemoryRegionsList &memory)
{
    const uint32_t count      = static_cast<uint32_t>(targets.size());
    const DeviceInterface &vk = getDeviceInterface();
    const VkDevice vkDevice   = getDevice();

    memory.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        VkMemoryRequirements memReqs;
        vk.getImageMemoryRequirements(vkDevice, **targets[i], &memReqs);

#ifdef CTS_USES_VULKANSC
        const VkMemoryAllocateInfo memAlloc = makeMemoryAllocateInfo(memReqs, DE_NULL);
#else
        VkMemoryPriorityAllocateInfoEXT priority = makeMemoryPriorityAllocateInfo(DE_NULL, ((float)i) / ((float)count));
        const VkMemoryAllocateInfo memAlloc =
            makeMemoryAllocateInfo(memReqs, (m_params.priorityMode == PRIORITY_MODE_STATIC) ? &priority : DE_NULL);
#endif

        VkDeviceMemory rawMemory = DE_NULL;

        vk.allocateMemory(vkDevice, &memAlloc, (VkAllocationCallbacks *)DE_NULL, &rawMemory);

#ifndef CTS_USES_VULKANSC
        if (m_params.priorityMode == PRIORITY_MODE_DYNAMIC)
            vk.setDeviceMemoryPriorityEXT(vkDevice, rawMemory, priority.priority);
#endif // CTS_USES_VULKANSC

        memory.push_back(de::SharedPtr<Move<VkDeviceMemory>>(new Move<VkDeviceMemory>(
            check<VkDeviceMemory>(rawMemory), Deleter<VkDeviceMemory>(vk, vkDevice, DE_NULL))));
    }
}

template <>
void BaseTestInstance::createMemory<VkBuffer, true>(BuffersList &targets, MemoryRegionsList &memory)
{
    const uint32_t count      = static_cast<uint32_t>(targets.size());
    const DeviceInterface &vk = getDeviceInterface();
    const VkDevice vkDevice   = getDevice();

    memory.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        VkMemoryRequirements memReqs;

        vk.getBufferMemoryRequirements(vkDevice, **targets[i], &memReqs);

        ConstDedicatedInfo dedicatedAllocationInfo = makeDedicatedAllocationInfo(**targets[i]);
#ifdef CTS_USES_VULKANSC
        const VkMemoryAllocateInfo memAlloc = makeMemoryAllocateInfo(memReqs, (const void *)&dedicatedAllocationInfo);
#else
        VkMemoryPriorityAllocateInfoEXT priority =
            makeMemoryPriorityAllocateInfo(&dedicatedAllocationInfo, ((float)i) / ((float)count));
        const VkMemoryAllocateInfo memAlloc = makeMemoryAllocateInfo(
            memReqs,
            (m_params.priorityMode == PRIORITY_MODE_STATIC) ? &priority : (const void *)&dedicatedAllocationInfo);
#endif

        VkDeviceMemory rawMemory = DE_NULL;

        vk.allocateMemory(vkDevice, &memAlloc, static_cast<VkAllocationCallbacks *>(DE_NULL), &rawMemory);

#ifndef CTS_USES_VULKANSC
        if (m_params.priorityMode == PRIORITY_MODE_DYNAMIC)
            vk.setDeviceMemoryPriorityEXT(vkDevice, rawMemory, priority.priority);
#endif // CTS_USES_VULKANSC

        memory.push_back(MemoryRegionPtr(new Move<VkDeviceMemory>(check<VkDeviceMemory>(rawMemory),
                                                                  Deleter<VkDeviceMemory>(vk, vkDevice, DE_NULL))));
    }
}

template <>
void BaseTestInstance::createMemory<VkImage, true>(ImagesList &targets, MemoryRegionsList &memory)
{
    const uint32_t count      = static_cast<uint32_t>(targets.size());
    const DeviceInterface &vk = getDeviceInterface();
    const VkDevice vkDevice   = getDevice();

    memory.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        VkMemoryRequirements memReqs;
        vk.getImageMemoryRequirements(vkDevice, **targets[i], &memReqs);

        ConstDedicatedInfo dedicatedAllocationInfo = makeDedicatedAllocationInfo(**targets[i]);

#ifdef CTS_USES_VULKANSC
        const VkMemoryAllocateInfo memAlloc = makeMemoryAllocateInfo(memReqs, (const void *)&dedicatedAllocationInfo);
#else
        VkMemoryPriorityAllocateInfoEXT priority =
            makeMemoryPriorityAllocateInfo(&dedicatedAllocationInfo, ((float)i) / ((float)count));
        const VkMemoryAllocateInfo memAlloc =
            makeMemoryAllocateInfo(memReqs, m_params.priorityMode ? &priority : (const void *)&dedicatedAllocationInfo);
#endif

        VkDeviceMemory rawMemory = DE_NULL;

        vk.allocateMemory(vkDevice, &memAlloc, static_cast<VkAllocationCallbacks *>(DE_NULL), &rawMemory);

#ifndef CTS_USES_VULKANSC
        if (m_params.priorityMode == PRIORITY_MODE_DYNAMIC)
            vk.setDeviceMemoryPriorityEXT(vkDevice, rawMemory, priority.priority);
#endif // CTS_USES_VULKANSC

        memory.push_back(MemoryRegionPtr(new Move<VkDeviceMemory>(check<VkDeviceMemory>(rawMemory),
                                                                  Deleter<VkDeviceMemory>(vk, vkDevice, DE_NULL))));
    }
}

template <>
void BaseTestInstance::makeBinding<VkBuffer>(BuffersList &targets, MemoryRegionsList &memory)
{
    const uint32_t count      = static_cast<uint32_t>(targets.size());
    const VkDevice vkDevice   = getDevice();
    const DeviceInterface &vk = getDeviceInterface();
    BindBufferMemoryInfosList bindMemoryInfos;
#ifndef CTS_USES_VULKANSC
    std::vector<VkResult> bindResults;
    BindMemoryStatusList bindMemoryStatus;

    if (m_params.checkIndividualResult)
    {
        bindResults.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            bindResults.push_back(VK_ERROR_UNKNOWN);
            bindMemoryStatus.push_back(makeBindMemoryStatus(&bindResults[i]));
        }
    }
#endif // CTS_USES_VULKANSC

    for (uint32_t i = 0; i < count; ++i)
    {
#ifndef CTS_USES_VULKANSC
        VkBindBufferMemoryInfo bindMemoryInfo = makeBufferMemoryBindingInfo(
            **targets[i], **memory[i], m_params.checkIndividualResult ? &bindMemoryStatus[i] : nullptr);
#else
        VkBindBufferMemoryInfo bindMemoryInfo = makeBufferMemoryBindingInfo(**targets[i], **memory[i], nullptr);
#endif // CTS_USES_VULKANSC
        bindMemoryInfos.push_back(bindMemoryInfo);
    }

    VK_CHECK(vk.bindBufferMemory2(vkDevice, count, &bindMemoryInfos.front()));
#ifndef CTS_USES_VULKANSC
    if (m_params.checkIndividualResult)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            VK_CHECK(*bindMemoryStatus[i].pResult);
        }
    }
#endif // CTS_USES_VULKANSC
}

template <>
void BaseTestInstance::makeBinding<VkImage>(ImagesList &targets, MemoryRegionsList &memory)
{
    const uint32_t count      = static_cast<uint32_t>(targets.size());
    const VkDevice vkDevice   = getDevice();
    const DeviceInterface &vk = getDeviceInterface();
    BindImageMemoryInfosList bindMemoryInfos;
#ifndef CTS_USES_VULKANSC
    std::vector<VkResult> bindResults;
    BindMemoryStatusList bindMemoryStatus;

    if (m_params.checkIndividualResult)
    {
        bindResults.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            bindResults.push_back(VK_ERROR_UNKNOWN);
            bindMemoryStatus.push_back(makeBindMemoryStatus(&bindResults[i]));
        }
    }
#endif // CTS_USES_VULKANSC

    for (uint32_t i = 0; i < count; ++i)
    {
#ifndef CTS_USES_VULKANSC
        VkBindImageMemoryInfo bindMemoryInfo = makeImageMemoryBindingInfo(
            **targets[i], **memory[i], m_params.checkIndividualResult ? &bindMemoryStatus[i] : nullptr);
#else
        VkBindImageMemoryInfo bindMemoryInfo  = makeImageMemoryBindingInfo(**targets[i], **memory[i], nullptr);
#endif // CTS_USES_VULKANSC
        bindMemoryInfos.push_back(bindMemoryInfo);
    }

    VK_CHECK(vk.bindImageMemory2(vkDevice, count, &bindMemoryInfos.front()));

#ifndef CTS_USES_VULKANSC
    if (m_params.checkIndividualResult)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            VK_CHECK(*bindMemoryStatus[i].pResult);
        }
    }
#endif // CTS_USES_VULKANSC
}

template <>
void BaseTestInstance::fillUpResource<VkBuffer>(Move<VkBuffer> &source, Move<VkBuffer> &target)
{
    const DeviceInterface &vk = getDeviceInterface();
    const VkDevice vkDevice   = getDevice();
    const VkQueue queue       = getUniversalQueue();

    const VkBufferMemoryBarrier srcBufferBarrier =
        makeMemoryBarrierInfo(*source, m_params.bufferSize, TransferFromResource);
    const VkBufferMemoryBarrier dstBufferBarrier =
        makeMemoryBarrierInfo(*target, m_params.bufferSize, TransferToResource);

    Move<VkCommandPool> commandPool = createCommandPool(vk, vkDevice, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, 0);
    Move<VkCommandBuffer> cmdBuffer = createCommandBuffer(vk, vkDevice, *commandPool);
    VkBufferCopy bufferCopy         = {0u, 0u, m_params.bufferSize};

    beginCommandBuffer(vk, *cmdBuffer);
    vk.cmdPipelineBarrier(*cmdBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, (VkDependencyFlags)0,
                          0, (const VkMemoryBarrier *)DE_NULL, 1, &srcBufferBarrier, 0,
                          (const VkImageMemoryBarrier *)DE_NULL);
    vk.cmdCopyBuffer(*cmdBuffer, *source, *target, 1, &bufferCopy);
    vk.cmdPipelineBarrier(*cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, (VkDependencyFlags)0,
                          0, (const VkMemoryBarrier *)DE_NULL, 1, &dstBufferBarrier, 0,
                          (const VkImageMemoryBarrier *)DE_NULL);
    endCommandBuffer(vk, *cmdBuffer);

    submitCommandsAndWait(vk, vkDevice, queue, *cmdBuffer);
}

template <>
void BaseTestInstance::fillUpResource<VkImage>(Move<VkBuffer> &source, Move<VkImage> &target)
{
    const DeviceInterface &vk = getDeviceInterface();
    const VkDevice vkDevice   = getDevice();
    const VkQueue queue       = getUniversalQueue();

    const VkBufferMemoryBarrier srcBufferBarrier =
        makeMemoryBarrierInfo(*source, m_params.bufferSize, TransferFromResource);
    const VkImageMemoryBarrier preImageBarrier = makeMemoryBarrierInfo(
        *target, 0u, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkImageMemoryBarrier dstImageBarrier =
        makeMemoryBarrierInfo(*target, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    Move<VkCommandPool> commandPool = createCommandPool(vk, vkDevice, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, 0);
    Move<VkCommandBuffer> cmdBuffer = createCommandBuffer(vk, vkDevice, *commandPool);

    const VkBufferImageCopy copyRegion = {
        0u,                        // VkDeviceSize bufferOffset;
        m_params.imageSize.width,  // uint32_t bufferRowLength;
        m_params.imageSize.height, // uint32_t bufferImageHeight;
        {
            VK_IMAGE_ASPECT_COLOR_BIT, // VkImageAspectFlags aspect;
            0u,                        // uint32_t mipLevel;
            0u,                        // uint32_t baseArrayLayer;
            1u,                        // uint32_t layerCount;
        },                             // VkImageSubresourceLayers imageSubresource;
        {0, 0, 0},                     // VkOffset3D imageOffset;
        m_params.imageSize             // VkExtent3D imageExtent;
    };

    beginCommandBuffer(vk, *cmdBuffer);
    vk.cmdPipelineBarrier(*cmdBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, (VkDependencyFlags)0,
                          0, (const VkMemoryBarrier *)DE_NULL, 1, &srcBufferBarrier, 1, &preImageBarrier);
    vk.cmdCopyBufferToImage(*cmdBuffer, *source, *target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, (&copyRegion));
    vk.cmdPipelineBarrier(*cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          (VkDependencyFlags)0, 0, (const VkMemoryBarrier *)DE_NULL, 0,
                          (const VkBufferMemoryBarrier *)DE_NULL, 1, &dstImageBarrier);
    endCommandBuffer(vk, *cmdBuffer);

    submitCommandsAndWait(vk, vkDevice, queue, *cmdBuffer);
}

template <>
void BaseTestInstance::readUpResource(Move<VkBuffer> &source, Move<VkBuffer> &target)
{
    fillUpResource(source, target);
}

template <>
void BaseTestInstance::readUpResource(Move<VkImage> &source, Move<VkBuffer> &target)
{
    const DeviceInterface &vk = getDeviceInterface();
    const VkDevice vkDevice   = getDevice();
    const VkQueue queue       = getUniversalQueue();

    Move<VkCommandPool> commandPool = createCommandPool(vk, vkDevice, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, 0);
    Move<VkCommandBuffer> cmdBuffer = createCommandBuffer(vk, vkDevice, *commandPool);

    beginCommandBuffer(vk, *cmdBuffer);
    copyImageToBuffer(vk, *cmdBuffer, *source, *target, tcu::IVec2(m_params.imageSize.width, m_params.imageSize.height),
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    endCommandBuffer(vk, *cmdBuffer);

    submitCommandsAndWait(vk, vkDevice, queue, *cmdBuffer);
}

template <>
void BaseTestInstance::layoutTransitionResource(Move<VkBuffer> &target)
{
    DE_UNREF(target);
}

template <>
void BaseTestInstance::layoutTransitionResource<VkImage>(Move<VkImage> &target)
{
    const DeviceInterface &vk = getDeviceInterface();
    const VkDevice vkDevice   = getDevice();
    const VkQueue queue       = getUniversalQueue();

    const VkImageMemoryBarrier preImageBarrier = makeMemoryBarrierInfo(
        *target, 0u, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    Move<VkCommandPool> commandPool = createCommandPool(vk, vkDevice, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, 0);
    Move<VkCommandBuffer> cmdBuffer = createCommandBuffer(vk, vkDevice, *commandPool);

    beginCommandBuffer(vk, *cmdBuffer);
    vk.cmdPipelineBarrier(*cmdBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, (VkDependencyFlags)0,
                          0, (const VkMemoryBarrier *)DE_NULL, 0, (const VkBufferMemoryBarrier *)DE_NULL, 1,
                          &preImageBarrier);
    endCommandBuffer(vk, *cmdBuffer);

    submitCommandsAndWait(vk, vkDevice, queue, *cmdBuffer);
}

void BaseTestInstance::createBuffer(Move<VkBuffer> &buffer, Move<VkDeviceMemory> &memory, VkDeviceSize *memorySize)
{
    const DeviceInterface &vk       = getDeviceInterface();
    const VkDevice vkDevice         = getDevice();
    VkBufferCreateInfo bufferParams = makeBufferCreateInfo(m_context, m_params);
    VkMemoryRequirements memReqs;

    buffer = vk::createBuffer(vk, vkDevice, &bufferParams);
    vk.getBufferMemoryRequirements(vkDevice, *buffer, &memReqs);
    *memorySize = memReqs.size;

    const VkMemoryAllocateInfo memAlloc = makeMemoryAllocateInfo(m_context, memReqs, MemoryHostVisible);
    VkDeviceMemory rawMemory            = DE_NULL;

    vk.allocateMemory(vkDevice, &memAlloc, static_cast<VkAllocationCallbacks *>(DE_NULL), &rawMemory);
    memory = Move<VkDeviceMemory>(check<VkDeviceMemory>(rawMemory), Deleter<VkDeviceMemory>(vk, vkDevice, DE_NULL));
    VK_CHECK(vk.bindBufferMemory(vkDevice, *buffer, *memory, 0u));
}

void BaseTestInstance::pushData(VkDeviceMemory memory, uint32_t dataSeed, VkDeviceSize size)
{
    const DeviceInterface &vk = getDeviceInterface();
    const VkDevice vkDevice   = getDevice();
    MemoryMappingRAII hostMemory(vk, vkDevice, memory, 0u, size, 0u);
    uint8_t *hostBuffer = static_cast<uint8_t *>(hostMemory.ptr());
    SimpleRandomGenerator random(dataSeed);

    for (uint32_t i = 0u; i < size; ++i)
    {
        hostBuffer[i] = static_cast<uint8_t>(random.getNext() & 0xFFu);
    }
    hostMemory.flush();
}

bool BaseTestInstance::checkData(VkDeviceMemory memory, uint32_t dataSeed, VkDeviceSize size)
{
    const DeviceInterface &vk = getDeviceInterface();
    const VkDevice vkDevice   = getDevice();
    MemoryMappingRAII hostMemory(vk, vkDevice, memory, 0u, size, 0u);
    uint8_t *hostBuffer = static_cast<uint8_t *>(hostMemory.ptr());
    SimpleRandomGenerator random(dataSeed);

    hostMemory.invalidate();

    for (uint32_t i = 0u; i < m_params.bufferSize; ++i)
    {
        if (hostBuffer[i] != static_cast<uint8_t>(random.getNext() & 0xFFu))
            return false;
    }
    return true;
}

template <typename TTarget, bool TDedicated>
class MemoryBindingInstance : public BaseTestInstance
{
public:
    MemoryBindingInstance(Context &ctx, BindingCaseParameters params) : BaseTestInstance(ctx, params)
    {
    }

    virtual tcu::TestStatus iterate(void)
    {
        const InstanceInterface &vkInstance     = m_context.getInstanceInterface();
        const VkPhysicalDevice vkPhysicalDevice = m_context.getPhysicalDevice();
        VkPhysicalDeviceProperties properties;
        vkInstance.getPhysicalDeviceProperties(vkPhysicalDevice, &properties);
        std::vector<de::SharedPtr<Move<TTarget>>> targets;
        MemoryRegionsList memory;

        createBindingTargets<TTarget>(targets);
        createMemory<TTarget, TDedicated>(targets, memory);
        makeBinding<TTarget>(targets, memory);

        Move<VkBuffer> srcBuffer;
        Move<VkDeviceMemory> srcMemory;
        VkDeviceSize srcMemorySize;

        createBuffer(srcBuffer, srcMemory, &srcMemorySize);
        pushData(*srcMemory, 1, srcMemorySize);

        Move<VkBuffer> dstBuffer;
        Move<VkDeviceMemory> dstMemory;
        VkDeviceSize dstMemorySize;

        createBuffer(dstBuffer, dstMemory, &dstMemorySize);

        bool passed = true;
        for (uint32_t i = 0; i < m_params.targetsCount; ++i)
        {
            fillUpResource(srcBuffer, *targets[i]);
            readUpResource(*targets[i], dstBuffer);
            passed = checkData(*dstMemory, 1, dstMemorySize) && passed;
        }

        return passed ? tcu::TestStatus::pass("Pass") : tcu::TestStatus::fail("Failed");
    }
};

template <typename TTarget, bool TDedicated>
class AliasedMemoryBindingInstance : public BaseTestInstance
{
public:
    AliasedMemoryBindingInstance(Context &ctx, BindingCaseParameters params) : BaseTestInstance(ctx, params)
    {
    }

    virtual tcu::TestStatus iterate(void)
    {
        const InstanceInterface &vkInstance     = m_context.getInstanceInterface();
        const VkPhysicalDevice vkPhysicalDevice = m_context.getPhysicalDevice();
        VkPhysicalDeviceProperties properties;
        vkInstance.getPhysicalDeviceProperties(vkPhysicalDevice, &properties);
        std::vector<de::SharedPtr<Move<TTarget>>> targets[2];
        MemoryRegionsList memory;

        for (uint32_t i = 0; i < DE_LENGTH_OF_ARRAY(targets); ++i)
            createBindingTargets<TTarget>(targets[i]);
        createMemory<TTarget, TDedicated>(targets[0], memory);
        for (uint32_t i = 0; i < DE_LENGTH_OF_ARRAY(targets); ++i)
            makeBinding<TTarget>(targets[i], memory);

        Move<VkBuffer> srcBuffer;
        Move<VkDeviceMemory> srcMemory;
        VkDeviceSize srcMemorySize;

        createBuffer(srcBuffer, srcMemory, &srcMemorySize);
        pushData(*srcMemory, 2, srcMemorySize);

        Move<VkBuffer> dstBuffer;
        Move<VkDeviceMemory> dstMemory;
        VkDeviceSize dstMemorySize;

        createBuffer(dstBuffer, dstMemory, &dstMemorySize);

        bool passed = true;
        for (uint32_t i = 0; i < m_params.targetsCount; ++i)
        {
            // Do a layout transition on alias 1 before we transition and write to alias 0
            layoutTransitionResource(*(targets[1][i]));
            fillUpResource(srcBuffer, *(targets[0][i]));
            readUpResource(*(targets[1][i]), dstBuffer);
            passed = checkData(*dstMemory, 2, dstMemorySize) && passed;
        }

        return passed ? tcu::TestStatus::pass("Pass") : tcu::TestStatus::fail("Failed");
    }
};

template <typename TInstance>
class MemoryBindingTest : public TestCase
{
public:
    MemoryBindingTest(tcu::TestContext &testCtx, const std::string &name, BindingCaseParameters params)
        : TestCase(testCtx, name)
        , m_params(params)
    {
    }

    virtual ~MemoryBindingTest(void)
    {
    }

    virtual TestInstance *createInstance(Context &ctx) const
    {
        return new TInstance(ctx, m_params);
    }

    virtual void checkSupport(Context &ctx) const
    {
        ctx.requireDeviceFunctionality("VK_KHR_bind_memory2");

#ifndef CTS_USES_VULKANSC
        if ((m_params.priorityMode != PRIORITY_MODE_DEFAULT) && !ctx.getMemoryPriorityFeaturesEXT().memoryPriority)
            TCU_THROW(NotSupportedError, "VK_EXT_memory_priority Not supported");
        if ((m_params.priorityMode == PRIORITY_MODE_DYNAMIC) &&
            !ctx.isDeviceFunctionalitySupported("VK_EXT_pageable_device_local_memory"))
            TCU_THROW(NotSupportedError, "VK_EXT_pageable_device_local_memory Not supported");
        if (m_params.checkIndividualResult)
            ctx.requireDeviceFunctionality("VK_KHR_maintenance6");
#endif
    }

private:
    BindingCaseParameters m_params;
};

} // unnamed namespace

tcu::TestCaseGroup *createMemoryBindingTests(tcu::TestContext &testCtx)
{
    de::MovePtr<tcu::TestCaseGroup> group(new tcu::TestCaseGroup(testCtx, "binding", "Memory binding tests."));
    de::MovePtr<tcu::TestCaseGroup> maint6(
        new tcu::TestCaseGroup(testCtx, "maintenance6", "Maintenance6 memory binding tests."));

#ifdef CTS_USES_VULKANSC
    const int iterations = 1;
#else
    const int iterations = 6;
#endif

    for (int i = 0; i < iterations; ++i)
    {
        PriorityMode priorityMode       = PriorityMode(i % 3);
        bool checkIndividualBindResults = i / 3;

        // Basic memory binding tests.
        de::MovePtr<tcu::TestCaseGroup> regular(new tcu::TestCaseGroup(testCtx, "regular"));
        // Memory binding tests with aliasing of two resources.
        de::MovePtr<tcu::TestCaseGroup> aliasing(new tcu::TestCaseGroup(testCtx, "aliasing"));

        de::MovePtr<tcu::TestCaseGroup> regular_suballocated(new tcu::TestCaseGroup(testCtx, "suballocated"));
        de::MovePtr<tcu::TestCaseGroup> regular_dedicated(new tcu::TestCaseGroup(testCtx, "dedicated"));

        de::MovePtr<tcu::TestCaseGroup> aliasing_suballocated(new tcu::TestCaseGroup(testCtx, "suballocated"));

        const VkDeviceSize allocationSizes[] = {33, 257, 4087, 8095, 1 * 1024 * 1024 + 1};

        for (uint32_t sizeNdx = 0u; sizeNdx < DE_LENGTH_OF_ARRAY(allocationSizes); ++sizeNdx)
        {
            const VkDeviceSize bufferSize      = allocationSizes[sizeNdx];
            const BindingCaseParameters params = makeBindingCaseParameters(
                10, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE,
                bufferSize, 0u, priorityMode, checkIndividualBindResults);
            const BindingCaseParameters aliasparams = makeBindingCaseParameters(
                10, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE,
                bufferSize, VK_IMAGE_CREATE_ALIAS_BIT, priorityMode, checkIndividualBindResults);
            std::ostringstream testName;

            testName << "buffer_" << bufferSize;
            regular_suballocated->addChild(
                new MemoryBindingTest<MemoryBindingInstance<VkBuffer, false>>(testCtx, testName.str(), params));
            regular_dedicated->addChild(
                new MemoryBindingTest<MemoryBindingInstance<VkBuffer, true>>(testCtx, testName.str(), params));
            aliasing_suballocated->addChild(new MemoryBindingTest<AliasedMemoryBindingInstance<VkBuffer, false>>(
                testCtx, testName.str(), aliasparams));
        }

        const uint32_t imageSizes[] = {8, 33, 257};

        for (uint32_t widthNdx = 0u; widthNdx < DE_LENGTH_OF_ARRAY(imageSizes); ++widthNdx)
            for (uint32_t heightNdx = 0u; heightNdx < DE_LENGTH_OF_ARRAY(imageSizes); ++heightNdx)
            {
                const uint32_t width  = imageSizes[widthNdx];
                const uint32_t height = imageSizes[heightNdx];
                const BindingCaseParameters regularparams =
                    makeBindingCaseParameters(10, width, height, 0u, priorityMode, checkIndividualBindResults);
                const BindingCaseParameters aliasparams = makeBindingCaseParameters(
                    10, width, height, VK_IMAGE_CREATE_ALIAS_BIT, priorityMode, checkIndividualBindResults);
                std::ostringstream testName;

                testName << "image_" << width << '_' << height;
                regular_suballocated->addChild(new MemoryBindingTest<MemoryBindingInstance<VkImage, false>>(
                    testCtx, testName.str(), regularparams));
                regular_dedicated->addChild(new MemoryBindingTest<MemoryBindingInstance<VkImage, true>>(
                    testCtx, testName.str(), regularparams));
                aliasing_suballocated->addChild(new MemoryBindingTest<AliasedMemoryBindingInstance<VkImage, false>>(
                    testCtx, testName.str(), aliasparams));
            }

        regular->addChild(regular_suballocated.release());
        regular->addChild(regular_dedicated.release());

        aliasing->addChild(aliasing_suballocated.release());

        tcu::TestCaseGroup *parent = checkIndividualBindResults ? maint6.get() : group.get();
        if (priorityMode != PRIORITY_MODE_DEFAULT)
        {
            de::MovePtr<tcu::TestCaseGroup> priority(new tcu::TestCaseGroup(
                testCtx, (priorityMode == PRIORITY_MODE_DYNAMIC) ? "priority_dynamic" : "priority",
                (priorityMode == PRIORITY_MODE_DYNAMIC) ? "Using VK_EXT_pageable_device_local_memory" :
                                                          "Using VK_EXT_memory_priority."));
            priority->addChild(regular.release());
            priority->addChild(aliasing.release());
            parent->addChild(priority.release());
        }
        else
        {
            parent->addChild(regular.release());
            parent->addChild(aliasing.release());
        }
    }
    group->addChild(maint6.release());
    return group.release();
}

} // namespace memory
} // namespace vkt
