// Diagnostic-only GPU test. Executes synthesized Xenos microcode through the
// same SPIR-V compute translator used by the Mali vertex-export fallback.
#include <algorithm>
#include <android/log.h>
#include <array>
#include <bit>
#include <cstring>
#include <future>
#include <rex/cvar.h>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/graphics/pipeline/shader/spirv.h>
#include <rex/memory/utils.h>
#include <rex/ui/vulkan/util.h>

namespace {
using Device = rex::ui::vulkan::VulkanDevice;
constexpr uint32_t kTestCode[] = {0,
                                  0x2001c600,
                                  0x28000000,
                                  32u | (1u << 15) | (15u << 16) | (50u << 26),
                                  0x006c0000,
                                  (11u << 24) | (1u << 31) | (2u << 8),
                                  33u | (1u << 15) | (15u << 16) | (50u << 26),
                                  0,
                                  (2u << 24) | (1u << 16) | (1u << 8)};
struct Resources {
  const Device *device;
  VkBuffer storage{}, constants{};
  VkDeviceMemory storage_memory{}, constants_memory{};
  void *storage_data{};
  void *constants_data{};
  VkShaderModule module{};
  std::array<VkDescriptorSetLayout, 4> layouts{};
  VkPipelineLayout pipeline_layout{};
  VkPipeline pipeline{};
  VkDescriptorPool descriptors{};
  VkCommandPool commands{};
  VkFence fence{};
  bool submitted = false;
  ~Resources() {
    const auto &f = device->functions();
    auto d = device->device();
    if (submitted)
      f.vkWaitForFences(d, 1, &fence, VK_TRUE, UINT64_MAX);
    if (fence)
      f.vkDestroyFence(d, fence, nullptr);
    if (commands)
      f.vkDestroyCommandPool(d, commands, nullptr);
    if (pipeline)
      f.vkDestroyPipeline(d, pipeline, nullptr);
    if (pipeline_layout)
      f.vkDestroyPipelineLayout(d, pipeline_layout, nullptr);
    if (descriptors)
      f.vkDestroyDescriptorPool(d, descriptors, nullptr);
    for (auto layout : layouts)
      if (layout)
        f.vkDestroyDescriptorSetLayout(d, layout, nullptr);
    if (module)
      f.vkDestroyShaderModule(d, module, nullptr);
    if (storage_data)
      f.vkUnmapMemory(d, storage_memory);
    if (constants_data)
      f.vkUnmapMemory(d, constants_memory);
    if (storage)
      f.vkDestroyBuffer(d, storage, nullptr);
    if (constants)
      f.vkDestroyBuffer(d, constants, nullptr);
    if (storage_memory)
      f.vkFreeMemory(d, storage_memory, nullptr);
    if (constants_memory)
      f.vkFreeMemory(d, constants_memory, nullptr);
  }
};
#define CHECK_GPU(condition)                                                   \
  do {                                                                         \
    if (!(condition)) {                                                        \
      __android_log_print(ANDROID_LOG_ERROR, "BurstLimitProbe",                \
                          "Compute export test failed at line %d", __LINE__);  \
      return false;                                                            \
    }                                                                          \
  } while (false)
} // namespace

extern "C" __attribute__((visibility("default"))) bool
burstlimit_test_compute_memexport(const Device *device) {
  using namespace rex::graphics;
  using namespace rex::ui::vulkan;
  // alloc memory 1; exece 2 ALU instructions at address 1.
  // mad eA, r0.xxxx, c2, c0; max eM0, c1, c1.
  // c0 encodes an 8-element float4 export stream; c2 advances its index.
  const auto &code = kTestCode;
  SpirvShader shader(xenos::ShaderType::kVertex, 1, code, std::size(code),
                     std::endian::little);
  rex::string::StringBuffer disassembly;
  shader.AnalyzeUcode(disassembly);
  CHECK_GPU(shader.memexport_eM_written() == 1);
  SpirvShaderTranslator translator(SpirvShaderTranslator::Features(device),
                                   false, false, false);
  auto *translation = shader.GetOrCreateTranslation(
      translator.GetDefaultVertexShaderModification(
          1, Shader::HostVertexShaderType::kMemExportCompute));
  CHECK_GPU(translator.TranslateAnalyzedShader(*translation) &&
            translation->is_valid());
  Resources r{device};
  const auto &f = device->functions();
  VkDevice d = device->device();
  VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  module.codeSize = translation->translated_binary().size();
  module.pCode = reinterpret_cast<const uint32_t *>(
      translation->translated_binary().data());
  CHECK_GPU(f.vkCreateShaderModule(d, &module, nullptr, &r.module) ==
            VK_SUCCESS);

  uint32_t storage_type{}, constants_type{};
  CHECK_GPU(util::CreateDedicatedAllocationBuffer(
      device, 4096, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      util::MemoryPurpose::kReadback, r.storage, r.storage_memory,
      &storage_type));
  CHECK_GPU(util::CreateDedicatedAllocationBuffer(
      device, 5 * 16384, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      util::MemoryPurpose::kUpload, r.constants, r.constants_memory,
      &constants_type));
  CHECK_GPU(f.vkMapMemory(d, r.storage_memory, 0, VK_WHOLE_SIZE, 0,
                          &r.storage_data) == VK_SUCCESS);
  CHECK_GPU(f.vkMapMemory(d, r.constants_memory, 0, VK_WHOLE_SIZE, 0,
                          &r.constants_data) == VK_SUCCESS);
  std::memset(r.constants_data, 0, 5 * 16384);
  const uint32_t stream[] = {0x40000040, 0x4b000000,
                             0x4b000000 | (38u << 8) | (7u << 16), 0x4b000008};
  const float payload[] = {1.25f, 2.5f, 3.75f, 4.0f};
  const float stride[] = {0, 1, 0, 0};
  auto *floats = static_cast<uint8_t *>(r.constants_data) + 16384;
  std::memcpy(floats, stream, 16);
  std::memcpy(floats + 16, payload, 16);
  std::memcpy(floats + 32, stride, 16);

  uint32_t storage_count =
      1u << translator.GetSharedMemoryStorageBufferCountLog2();
  const VkDescriptorSetLayoutBinding storage_bindings[] = {
      {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storage_count,
       VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
       nullptr}};
  std::array<VkDescriptorSetLayoutBinding, 5> constant_bindings{};
  for (uint32_t i = 0; i < 5; ++i)
    constant_bindings[i] = {i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                            VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  for (int set = 0; set < 4; ++set) {
    VkDescriptorSetLayoutCreateInfo create{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    create.bindingCount = set == 0 ? 2 : set == 1 ? 5 : 0;
    create.pBindings = set == 0   ? storage_bindings
                       : set == 1 ? constant_bindings.data()
                                  : nullptr;
    CHECK_GPU(f.vkCreateDescriptorSetLayout(d, &create, nullptr,
                                            &r.layouts[set]) == VK_SUCCESS);
  }
  VkPipelineLayoutCreateInfo layout{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout.setLayoutCount = 4;
  layout.pSetLayouts = r.layouts.data();
  CHECK_GPU(f.vkCreatePipelineLayout(d, &layout, nullptr, &r.pipeline_layout) ==
            VK_SUCCESS);
  VkComputePipelineCreateInfo pipeline{
      VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pipeline.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  pipeline.stage.module = r.module;
  pipeline.stage.pName = "main";
  pipeline.layout = r.pipeline_layout;
  CHECK_GPU(f.vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &pipeline, nullptr,
                                       &r.pipeline) == VK_SUCCESS);
  const VkDescriptorPoolSize sizes[] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storage_count + 1},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 5}};
  VkDescriptorPoolCreateInfo pool{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool.maxSets = 2;
  pool.poolSizeCount = 2;
  pool.pPoolSizes = sizes;
  CHECK_GPU(f.vkCreateDescriptorPool(d, &pool, nullptr, &r.descriptors) ==
            VK_SUCCESS);
  VkDescriptorSetAllocateInfo allocate{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate.descriptorPool = r.descriptors;
  allocate.descriptorSetCount = 2;
  allocate.pSetLayouts = r.layouts.data();
  VkDescriptorSet sets[2];
  CHECK_GPU(f.vkAllocateDescriptorSets(d, &allocate, sets) == VK_SUCCESS);
  std::vector<VkDescriptorBufferInfo> storage_infos(storage_count,
                                                    {r.storage, 0, 4096});
  std::array<VkDescriptorBufferInfo, 5> constant_infos{};
  std::array<VkWriteDescriptorSet, 7> writes{};
  for (uint32_t i = 0; i < writes.size(); ++i) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = sets[i < 2 ? 0 : 1];
    writes[i].dstBinding = i < 2 ? i : i - 2;
    writes[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                     : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[i].descriptorCount = i == 0 ? storage_count : 1;
    if (i < 2)
      writes[i].pBufferInfo = storage_infos.data();
    else {
      constant_infos[i - 2] = {r.constants, (i - 2) * 16384, 16384};
      writes[i].pBufferInfo = &constant_infos[i - 2];
    }
  }
  f.vkUpdateDescriptorSets(d, writes.size(), writes.data(), 0, nullptr);
  VkCommandPoolCreateInfo commands{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  commands.queueFamilyIndex = device->queue_family_graphics_compute();
  CHECK_GPU(f.vkCreateCommandPool(d, &commands, nullptr, &r.commands) ==
            VK_SUCCESS);
  VkCommandBufferAllocateInfo command_allocate{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  command_allocate.commandPool = r.commands;
  command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_allocate.commandBufferCount = 1;
  VkCommandBuffer command;
  CHECK_GPU(f.vkAllocateCommandBuffers(d, &command_allocate, &command) ==
            VK_SUCCESS);
  VkFenceCreateInfo fence_create{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  CHECK_GPU(f.vkCreateFence(d, &fence_create, nullptr, &r.fence) == VK_SUCCESS);

  // Verify three index sources plus primitive reset. Extra dispatched threads
  // must leave memory unchanged because the guest count is only three.
  for (uint32_t mode = 0; mode < 4; ++mode) {
    std::memset(r.storage_data, 0xcd, 4096);
    SpirvShaderTranslator::SystemConstants system{};
    system.vertex_index_max = 0xffffff;
    system.compute_memexport_vertex_count = 3;
    std::array<uint32_t, 3> expected =
        mode == 0   ? std::array<uint32_t, 3>{0, 1, 2}
        : mode == 2 ? std::array<uint32_t, 3>{6, 1, 4}
                    : std::array<uint32_t, 3>{2, 0, 5};
    if (mode) {
      system.flags =
          SpirvShaderTranslator::kSysFlag_ComputeOrPrimitiveVertexIndexLoad;
      system.vertex_index_load_address = 16;
      system.vertex_index_endian =
          mode == 2 ? xenos::Endian::k8in32 : xenos::Endian::k8in16;
      if (mode == 2)
        system.flags |= SpirvShaderTranslator::
            kSysFlag_ComputeOrPrimitiveVertexIndexLoad32Bit;
      if (mode == 3) {
        system.flags |=
            SpirvShaderTranslator::kSysFlag_ComputeOrPrimitiveVertexIndexReset;
        system.vertex_index_reset = 0xffff;
        expected[1] = 0xffff;
      }
      auto *indices = static_cast<uint8_t *>(r.storage_data) + 16;
      for (auto index : expected) {
        if (mode == 2) {
          *indices++ = uint8_t(index >> 24);
          *indices++ = uint8_t(index >> 16);
        }
        *indices++ = uint8_t(index >> 8);
        *indices++ = uint8_t(index);
      }
    }
    std::memcpy(r.constants_data, &system, sizeof(system));
    util::FlushMappedMemoryRange(device, r.storage_memory, storage_type);
    util::FlushMappedMemoryRange(device, r.constants_memory, constants_type);
    CHECK_GPU(f.vkResetCommandPool(d, r.commands, 0) == VK_SUCCESS);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK_GPU(f.vkBeginCommandBuffer(command, &begin) == VK_SUCCESS);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barrier.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    f.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier,
                           0, nullptr, 0, nullptr);
    f.vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
    f.vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                              r.pipeline_layout, 0, 2, sets, 0, nullptr);
    f.vkCmdDispatch(command, 8, 1, 1);
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    f.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0,
                           nullptr, 0, nullptr);
    CHECK_GPU(f.vkEndCommandBuffer(command) == VK_SUCCESS);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    {
      auto queue = device->AcquireQueue(commands.queueFamilyIndex, 0);
      CHECK_GPU(f.vkQueueSubmit(queue.queue(), 1, &submit, r.fence) ==
                VK_SUCCESS);
      r.submitted = true;
      CHECK_GPU(f.vkWaitForFences(d, 1, &r.fence, VK_TRUE, UINT64_MAX) ==
                VK_SUCCESS);
      r.submitted = false;
      CHECK_GPU(f.vkResetFences(d, 1, &r.fence) == VK_SUCCESS);
    }
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = r.storage_memory;
    range.size = VK_WHOLE_SIZE;
    CHECK_GPU(f.vkInvalidateMappedMemoryRanges(d, 1, &range) == VK_SUCCESS);
    auto *output = static_cast<uint8_t *>(r.storage_data) + 256;
    for (uint32_t slot = 0; slot < 8; ++slot) {
      bool written =
          std::find(expected.begin(), expected.end(), slot) != expected.end();
      if (written)
        CHECK_GPU(std::memcmp(output + slot * 16, payload, 16) == 0);
      else
        for (uint32_t byte = 0; byte < 16; ++byte)
          CHECK_GPU(output[slot * 16 + byte] == 0xcd);
    }
    __android_log_print(ANDROID_LOG_INFO, "BurstLimitProbe",
                        "Compute export index mode %u: PASS", mode);
  }
  return true;
}

extern "C" __attribute__((visibility("default"))) bool
burstlimit_test_gpu_worker(rex::system::IGraphicsSystem *gpu) {
  auto *graphics = static_cast<rex::graphics::GraphicsSystem *>(gpu);
  if (!graphics->command_processor())
    return false;
  auto completed = std::make_shared<std::promise<void>>();
  auto result = completed->get_future();
  graphics->command_processor()->CallInThread(
      [completed] { completed->set_value(); });
  return result.wait_for(std::chrono::seconds(20)) == std::future_status::ready;
}

extern "C" __attribute__((visibility("default"))) bool
burstlimit_test_memexport_draw(rex::system::IGraphicsSystem *gpu) {
  using namespace rex::graphics;
  auto *graphics = static_cast<GraphicsSystem *>(gpu);
  auto completed = std::make_shared<std::promise<bool>>();
  auto result = completed->get_future();
  graphics->command_processor()->CallInThread([graphics, completed] {
    auto *memory = graphics->memory();
    __android_log_print(ANDROID_LOG_INFO, "BurstLimitProbe",
                        "Draw: allocating guest physical memory");
    uint32_t allocation =
        memory->SystemHeapAlloc(65536, 65536, rex::memory::kSystemHeapPhysical);
    __android_log_print(ANDROID_LOG_INFO, "BurstLimitProbe",
                        "Draw: allocated %08x", allocation);
    if (!allocation) {
      completed->set_value(false);
      return;
    }
    if (rex::memory::GuestPtr(memory->virtual_membase(), allocation) !=
        memory->TranslateVirtual(allocation)) {
      completed->set_value(false);
      return;
    }
    uint32_t physical = memory->GetPhysicalAddress(allocation);
    auto *data = memory->TranslatePhysical(physical);
    __android_log_print(ANDROID_LOG_INFO, "BurstLimitProbe",
                        "Draw: initialized physical %08x", physical);
    bool valid = true;
    for (uint32_t mode = 0; mode < 3; ++mode) {
      // Write through the CPU alias so subsequent draws also exercise GPU
      // write-watch invalidation and shared-memory re-upload after CPU edits.
      std::memset(memory->TranslateVirtual(allocation), 0xcd, 65536);
      const std::array<uint32_t, 3> expected =
          mode == 0   ? std::array<uint32_t, 3>{0, 1, 2}
          : mode == 1 ? std::array<uint32_t, 3>{2, 0, 5}
                      : std::array<uint32_t, 3>{6, 1, 4};
      if (mode) {
        auto *indices = data + 128;
        for (uint32_t index : expected) {
          if (mode == 2) {
            *indices++ = uint8_t(index >> 24);
            *indices++ = uint8_t(index >> 16);
          }
          *indices++ = uint8_t(index >> 8);
          *indices++ = uint8_t(index);
        }
      }
      std::vector<uint32_t> packets;
      auto reg = [&](Register index, uint32_t value) {
        packets.push_back(xenos::MakePacketType0(uint16_t(index), 1));
        packets.push_back(value);
      };
      reg(XE_GPU_REG_VGT_MAX_VTX_INDX, 0xffffff);
      reg(XE_GPU_REG_SQ_PROGRAM_CNTL, 0);
      reg(XE_GPU_REG_RB_MODECONTROL, 0);
      reg(XE_GPU_REG_RB_SURFACE_INFO, 0);
      packets.push_back(
          xenos::MakePacketType0(XE_GPU_REG_SHADER_CONSTANT_000_X, 12));
      packets.insert(
          packets.end(),
          {0x40000000u | ((physical + 256) >> 2), 0x4b000000,
           0x4b000000 | (38u << 8) | (7u << 16), 0x4b000008,
           std::bit_cast<uint32_t>(1.25f), std::bit_cast<uint32_t>(2.5f),
           std::bit_cast<uint32_t>(3.75f), std::bit_cast<uint32_t>(4.0f), 0,
           std::bit_cast<uint32_t>(1.0f), 0, 0});
      packets.push_back(xenos::MakePacketType3(xenos::PM4_IM_LOAD_IMMEDIATE,
                                               2 + std::size(kTestCode)));
      packets.push_back(uint32_t(xenos::ShaderType::kVertex));
      packets.push_back(std::size(kTestCode));
      packets.insert(packets.end(), std::begin(kTestCode), std::end(kTestCode));
      reg::VGT_DRAW_INITIATOR draw{};
      draw.prim_type = xenos::PrimitiveType::kTriangleList;
      draw.source_select =
          mode ? xenos::SourceSelect::kDMA : xenos::SourceSelect::kAutoIndex;
      draw.index_size =
          mode == 2 ? xenos::IndexFormat::kInt32 : xenos::IndexFormat::kInt16;
      draw.num_indices = 3;
      packets.push_back(
          xenos::MakePacketType3(xenos::PM4_DRAW_INDX_2, mode ? 3 : 1));
      packets.push_back(draw.value);
      if (mode) {
        reg::VGT_DMA_SIZE dma{};
        dma.num_words = 3;
        dma.swap_mode =
            mode == 2 ? xenos::Endian::k8in32 : xenos::Endian::k8in16;
        packets.push_back(physical + 128);
        packets.push_back(dma.value);
      }
      uint32_t packet_address = physical + 4096;
      for (size_t index = 0; index < packets.size(); ++index)
        rex::memory::store_and_swap<uint32_t>(
            memory->TranslatePhysical(packet_address + index * 4),
            packets[index]);
      __android_log_print(ANDROID_LOG_INFO, "BurstLimitProbe",
                          "Draw: executing %zu packet words", packets.size());
      graphics->command_processor()->ExecutePacket(packet_address,
                                                   packets.size());
      const float payload[] = {1.25f, 2.5f, 3.75f, 4.0f};
      bool mode_valid = true;
      for (uint32_t slot = 0; slot < 8; ++slot) {
        auto *output = data + 256 + slot * 16;
        bool written =
            std::find(expected.begin(), expected.end(), slot) != expected.end();
        if (written)
          mode_valid &= std::memcmp(output, payload, 16) == 0;
        else
          for (uint32_t byte = 0; byte < 16; ++byte)
            mode_valid &= output[byte] == 0xcd;
      }
      valid &= mode_valid;
      if (!mode_valid) {
        for (uint32_t slot = 0; slot < 8; ++slot) {
          uint32_t words[4];
          std::memcpy(words, data + 256 + slot * 16, 16);
          __android_log_print(ANDROID_LOG_ERROR, "BurstLimitProbe",
                              "Draw output %u: %08x %08x %08x %08x", slot,
                              words[0], words[1], words[2], words[3]);
        }
      }
      __android_log_print(mode_valid ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
                          "BurstLimitProbe", "Xenos draw index mode %u: %s",
                          mode, mode_valid ? "PASS" : "FAIL");
    }
    // Retain the physical allocation until Runtime shutdown: GPU page caches
    // still refer to it, just as they do for a running game's allocations.
    completed->set_value(valid);
  });
  return result.wait_for(std::chrono::seconds(20)) ==
             std::future_status::ready &&
         result.get();
}
