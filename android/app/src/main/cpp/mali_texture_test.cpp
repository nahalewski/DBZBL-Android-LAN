// Compare production BC1/BC2/BC3/BC4 upload shaders against palette/tiling
// expectations. Diagnostic APK only; no game data or runtime texture overrides.
#include <algorithm>
#include <android/log.h>
#include <array>
#include <cstring>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/ui/vulkan/util.h>

namespace {
using Device = rex::ui::vulkan::VulkanDevice;
namespace shaders {
#include "../../../../../thirdparty/rexglue-sdk/src/graphics/shaders/vulkan_spirv/texture_load_dxt1_rgba8_cs.h"
#include "../../../../../thirdparty/rexglue-sdk/src/graphics/shaders/vulkan_spirv/texture_load_dxt3_rgba8_cs.h"
#include "../../../../../thirdparty/rexglue-sdk/src/graphics/shaders/vulkan_spirv/texture_load_dxt5_rgba8_cs.h"
#include "../../../../../thirdparty/rexglue-sdk/src/graphics/shaders/vulkan_spirv/texture_load_dxt5a_r8_cs.h"
} // namespace shaders
struct Resources {
  const Device *dev;
  VkBuffer src{}, dst{};
  VkDeviceMemory src_mem{}, dst_mem{};
  void *src_data{};
  void *dst_data{};
  VkShaderModule shader{};
  VkDescriptorSetLayout set_layout{};
  VkPipelineLayout layout{};
  VkPipeline pipeline{};
  VkDescriptorPool descriptors{};
  VkCommandPool commands{};
  VkFence fence{};
  bool submitted = false;
  ~Resources() {
    const auto &f = dev->functions();
    auto d = dev->device();
    if (submitted)
      f.vkWaitForFences(d, 1, &fence, VK_TRUE, UINT64_MAX);
    if (fence)
      f.vkDestroyFence(d, fence, nullptr);
    if (commands)
      f.vkDestroyCommandPool(d, commands, nullptr);
    if (pipeline)
      f.vkDestroyPipeline(d, pipeline, nullptr);
    if (layout)
      f.vkDestroyPipelineLayout(d, layout, nullptr);
    if (descriptors)
      f.vkDestroyDescriptorPool(d, descriptors, nullptr);
    if (set_layout)
      f.vkDestroyDescriptorSetLayout(d, set_layout, nullptr);
    if (shader)
      f.vkDestroyShaderModule(d, shader, nullptr);
    if (src_data)
      f.vkUnmapMemory(d, src_mem);
    if (dst_data)
      f.vkUnmapMemory(d, dst_mem);
    if (src)
      f.vkDestroyBuffer(d, src, nullptr);
    if (dst)
      f.vkDestroyBuffer(d, dst, nullptr);
    if (src_mem)
      f.vkFreeMemory(d, src_mem, nullptr);
    if (dst_mem)
      f.vkFreeMemory(d, dst_mem, nullptr);
  }
};
#define REQUIRE_TEX(c)                                                         \
  do {                                                                         \
    if (!(c)) {                                                                \
      __android_log_print(ANDROID_LOG_ERROR, "BurstLimitProbe",                \
                          "BC1 setup failed line %d", __LINE__);               \
      return false;                                                            \
    }                                                                          \
  } while (false)
} // namespace

static bool TestTextureDecode(const Device *dev, unsigned format) {
  using namespace rex::ui::vulkan;
  Resources r{dev};
  const auto &f = dev->functions();
  auto d = dev->device();
  VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  const uint32_t *codes[]{
      shaders::texture_load_dxt1_rgba8_cs, shaders::texture_load_dxt3_rgba8_cs,
      shaders::texture_load_dxt5_rgba8_cs, shaders::texture_load_dxt5a_r8_cs};
  const size_t sizes[]{sizeof(shaders::texture_load_dxt1_rgba8_cs),
                       sizeof(shaders::texture_load_dxt3_rgba8_cs),
                       sizeof(shaders::texture_load_dxt5_rgba8_cs),
                       sizeof(shaders::texture_load_dxt5a_r8_cs)};
  shader.codeSize = sizes[format];
  shader.pCode = codes[format];
  const uint32_t block_bytes = (format == 1 || format == 2) ? 16 : 8;
  const bool alpha_only = format == 3;
  const uint32_t host_bpp = alpha_only ? 1 : 4;
  REQUIRE_TEX(f.vkCreateShaderModule(d, &shader, nullptr, &r.shader) ==
              VK_SUCCESS);
  uint32_t src_type{}, dst_type{};
  REQUIRE_TEX(util::CreateDedicatedAllocationBuffer(
      dev, 131072, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      util::MemoryPurpose::kUpload, r.src, r.src_mem, &src_type));
  REQUIRE_TEX(util::CreateDedicatedAllocationBuffer(
      dev, 65536, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      util::MemoryPurpose::kReadback, r.dst, r.dst_mem, &dst_type));
  REQUIRE_TEX(f.vkMapMemory(d, r.src_mem, 0, VK_WHOLE_SIZE, 0, &r.src_data) ==
              VK_SUCCESS);
  REQUIRE_TEX(f.vkMapMemory(d, r.dst_mem, 0, VK_WHOLE_SIZE, 0, &r.dst_data) ==
              VK_SUCCESS);
  VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                       VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo set{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  set.bindingCount = 1;
  set.pBindings = &binding;
  REQUIRE_TEX(f.vkCreateDescriptorSetLayout(d, &set, nullptr, &r.set_layout) ==
              VK_SUCCESS);
  const VkDescriptorSetLayout layouts[]{r.set_layout, r.set_layout};
  VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 40};
  VkPipelineLayoutCreateInfo layout{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout.setLayoutCount = 2;
  layout.pSetLayouts = layouts;
  layout.pushConstantRangeCount = 1;
  layout.pPushConstantRanges = &push;
  REQUIRE_TEX(f.vkCreatePipelineLayout(d, &layout, nullptr, &r.layout) ==
              VK_SUCCESS);
  VkComputePipelineCreateInfo pipeline{
      VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pipeline.layout = r.layout;
  pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  pipeline.stage.module = r.shader;
  pipeline.stage.pName = "main";
  REQUIRE_TEX(f.vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &pipeline,
                                         nullptr, &r.pipeline) == VK_SUCCESS);
  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
  VkDescriptorPoolCreateInfo pool{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool.maxSets = 2;
  pool.poolSizeCount = 1;
  pool.pPoolSizes = &size;
  REQUIRE_TEX(f.vkCreateDescriptorPool(d, &pool, nullptr, &r.descriptors) ==
              VK_SUCCESS);
  VkDescriptorSetAllocateInfo alloc{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  alloc.descriptorPool = r.descriptors;
  alloc.descriptorSetCount = 2;
  alloc.pSetLayouts = layouts;
  VkDescriptorSet sets[2];
  REQUIRE_TEX(f.vkAllocateDescriptorSets(d, &alloc, sets) == VK_SUCCESS);
  VkDescriptorBufferInfo buffers[]{{r.dst, 0, 65536}, {r.src, 0, 131072}};
  std::array<VkWriteDescriptorSet, 2> writes{};
  for (unsigned i = 0; i < 2; ++i) {
    writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[i].dstSet = sets[i];
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[i].descriptorCount = 1;
    writes[i].pBufferInfo = &buffers[i];
  }
  f.vkUpdateDescriptorSets(d, 2, writes.data(), 0, nullptr);
  VkCommandPoolCreateInfo commands{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  commands.queueFamilyIndex = dev->queue_family_graphics_compute();
  REQUIRE_TEX(f.vkCreateCommandPool(d, &commands, nullptr, &r.commands) ==
              VK_SUCCESS);
  VkCommandBufferAllocateInfo command_alloc{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  command_alloc.commandPool = r.commands;
  command_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_alloc.commandBufferCount = 1;
  VkCommandBuffer cmd;
  REQUIRE_TEX(f.vkAllocateCommandBuffers(d, &command_alloc, &cmd) ==
              VK_SUCCESS);
  VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  REQUIRE_TEX(f.vkCreateFence(d, &fence, nullptr, &r.fence) == VK_SUCCESS);
  for (unsigned mode = 0; mode < 4; ++mode) {
    std::memset(r.src_data, 0, 131072);
    std::memset(r.dst_data, 0xcd, 65536);
    std::array<uint32_t, 16384> expected{};
    for (uint32_t by = 0; by < 16; ++by)
      for (uint32_t bx = 0; bx < 64; ++bx) {
        bool alpha = format == 0 && ((bx + by) & 1) != 0;
        uint32_t endpoints = alpha ? 0xf80007e0u : 0x07e0f800u;
        uint32_t selectors = 0;
        uint64_t alpha_bits = 0;
        for (uint32_t y = 0; y < 4; ++y)
          for (uint32_t x = 0; x < 4; ++x) {
            uint32_t selector = (x + y + bx + by) & 3;
            selectors |= selector << ((y * 4 + x) * 2);
            constexpr uint32_t opaque[]{0xff0000ff, 0xff00ff00, 0xff0055aa,
                                        0xff00aa55};
            constexpr uint32_t transparent[]{0xff00ff00, 0xff0000ff, 0xff007f7f,
                                             0};
            uint32_t value = alpha ? transparent[selector] : opaque[selector];
            unsigned ai = (x + y + bx + by) & (format == 1 ? 15 : 7);
            if (format == 1) {
              alpha_bits |= uint64_t(ai) << ((y * 4 + x) * 4);
              value = (value & 0xffffff) | ((ai * 17) << 24);
            }
            if (format >= 2) {
              constexpr uint8_t ap[]{255, 0, 218, 182, 145, 109, 72, 36};
              alpha_bits |= uint64_t(ai) << ((y * 4 + x) * 3);
              value = alpha_only
                          ? ap[ai]
                          : ((value & 0xffffff) | (uint32_t(ap[ai]) << 24));
            }
            expected[(by * 4 + y) * 256 + bx * 4 + x] = value;
          }
        if (format >= 2)
          alpha_bits = (alpha_bits << 16) | 255u;
        uint32_t block[4]{};
        if (format == 0) {
          block[0] = endpoints;
          block[1] = selectors;
        } else {
          block[0] = uint32_t(alpha_bits);
          block[1] = uint32_t(alpha_bits >> 32);
          block[2] = endpoints;
          block[3] = selectors;
        }
        if (mode & 1)
          for (auto &word : block)
            word = __builtin_bswap32(word);
        uint32_t offset =
            mode >= 2 ? uint32_t(rex::graphics::texture_util::GetTiledOffset2D(
                            bx, by, 64, block_bytes == 8 ? 3 : 4))
                      : (by * 64 + bx) * block_bytes;
        std::memcpy(static_cast<uint8_t *>(r.src_data) + offset, block,
                    block_bytes);
      }
    uint32_t constants[]{0x90u | (mode >= 2 ? 1u : 0u) | ((mode & 1) ? 8u : 0u),
                         0,
                         mode >= 2 ? 64u : 64 * block_bytes,
                         16,
                         64,
                         16,
                         1,
                         0,
                         256 * host_bpp,
                         64};
    util::FlushMappedMemoryRange(dev, r.src_mem, src_type);
    util::FlushMappedMemoryRange(dev, r.dst_mem, dst_type);
    REQUIRE_TEX(f.vkResetCommandPool(d, r.commands, 0) == VK_SUCCESS);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    REQUIRE_TEX(f.vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barrier.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    f.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier,
                           0, nullptr, 0, nullptr);
    f.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
    f.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r.layout, 0,
                              2, sets, 0, nullptr);
    f.vkCmdPushConstants(cmd, r.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 40,
                         constants);
    f.vkCmdDispatch(cmd, block_bytes == 8 ? 4 : 8, 1, 1);
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    f.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0,
                           nullptr, 0, nullptr);
    REQUIRE_TEX(f.vkEndCommandBuffer(cmd) == VK_SUCCESS);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    {
      auto queue = dev->AcquireQueue(commands.queueFamilyIndex, 0);
      REQUIRE_TEX(f.vkQueueSubmit(queue.queue(), 1, &submit, r.fence) ==
                  VK_SUCCESS);
      r.submitted = true;
      REQUIRE_TEX(f.vkWaitForFences(d, 1, &r.fence, VK_TRUE, UINT64_MAX) ==
                  VK_SUCCESS);
      r.submitted = false;
    }
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = r.dst_mem;
    range.size = VK_WHOLE_SIZE;
    REQUIRE_TEX(f.vkInvalidateMappedMemoryRanges(d, 1, &range) == VK_SUCCESS);
    auto *actual = static_cast<uint32_t *>(r.dst_data);
    uint32_t bad = 0;
    for (uint32_t i = 0; i < expected.size(); ++i)
      if ((alpha_only ? static_cast<uint8_t *>(r.dst_data)[i] : actual[i]) !=
          expected[i]) {
        if (bad++ < 4)
          __android_log_print(
              ANDROID_LOG_ERROR, "BurstLimitProbe",
              "Texture format %u mode %u texel %u actual %08X expected %08X",
              format, mode, i,
              alpha_only ? static_cast<uint8_t *>(r.dst_data)[i] : actual[i],
              expected[i]);
      }
    __android_log_print(ANDROID_LOG_INFO, "BurstLimitProbe",
                        "Texture format %u mode %u mismatches %u/16384", format,
                        mode, bad);
    REQUIRE_TEX(f.vkResetFences(d, 1, &r.fence) == VK_SUCCESS);
    if (bad)
      return false;
  }
  return true;
}

extern "C" __attribute__((visibility("default"))) bool
burstlimit_test_texture_decode(const Device *dev) {
  bool ok = true;
  for (unsigned format = 0; format < 4; ++format)
    ok = TestTextureDecode(dev, format) && ok;
  return ok;
}
