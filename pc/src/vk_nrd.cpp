/* NVIDIA NRD RELAX diffuse on the Vulkan device (vk_nrd.h), following
 * NVIDIA's RELAX sample usage: NRD describes its pipelines
 * (embedded SPIR-V), resource pools and per-frame dispatches; this file
 * creates them on our device and records the dispatches into the frame's
 * command buffer. NRD's binding offsets come from its library description.
 * Both descriptor sets of a dispatch (resources; constant buffer + static
 * samplers) are allocated from a pool per frame slot (reset when the slot
 * comes round); the constants live in the frame's upload arena.
 *
 * Not push descriptors for the constants/samplers set: with them, NRD's
 * history stopped accumulating (history length stuck at 1) whenever NRD's
 * images were re-created after it had run once (a GI resolution change),
 * while the first instance worked; allocated sets work
 * in both cases (no validation messages either way). */
#include "vk_nrd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL3/SDL_timer.h>

extern "C" char pc_nrd_tune[160];
char pc_nrd_tune[160];
#ifdef ROOD_NRD
#include <NRD.h>

#include <vector>

namespace {
struct Pipeline {
    uint32_t bindings = 0;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout resources = VK_NULL_HANDLE;
};
struct State {
    nrd::Instance* instance = nullptr;
    int width = 0, height = 0;
    int failed = 0;
    std::vector<Pipeline> pipelines;
    std::vector<vk_image> permanent, transient;
    VkSampler samplers[8] = {};
    unsigned sampler_count = 0;
    VkDescriptorSetLayout constants = VK_NULL_HANDLE; /* CB + immutable samplers */
    uint32_t constants_binding = 0, resources_set = 0, constants_set = 1;
    uint32_t texture_base = 0, storage_base = 0;
    VkDescriptorPool pools[VK_FRAMES] = {};
    uint64_t pool_serial[VK_FRAMES] = {};
    char version[32] = "";
    struct timespec last_time = {};
} nrd_state;

VkFormat vk_format(nrd::Format f)
{
    switch (f) {
    case nrd::Format::R8_UNORM: return VK_FORMAT_R8_UNORM;
    case nrd::Format::R8_SNORM: return VK_FORMAT_R8_SNORM;
    case nrd::Format::R8_UINT: return VK_FORMAT_R8_UINT;
    case nrd::Format::RG8_UNORM: return VK_FORMAT_R8G8_UNORM;
    case nrd::Format::RG8_SNORM: return VK_FORMAT_R8G8_SNORM;
    case nrd::Format::RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
    case nrd::Format::RGBA8_SNORM: return VK_FORMAT_R8G8B8A8_SNORM;
    case nrd::Format::R16_UNORM: return VK_FORMAT_R16_UNORM;
    case nrd::Format::R16_UINT: return VK_FORMAT_R16_UINT;
    case nrd::Format::R16_SFLOAT: return VK_FORMAT_R16_SFLOAT;
    case nrd::Format::RG16_UNORM: return VK_FORMAT_R16G16_UNORM;
    case nrd::Format::RG16_SFLOAT: return VK_FORMAT_R16G16_SFLOAT;
    case nrd::Format::RGBA16_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;
    case nrd::Format::RGBA16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case nrd::Format::R32_UINT: return VK_FORMAT_R32_UINT;
    case nrd::Format::R32_SFLOAT: return VK_FORMAT_R32_SFLOAT;
    case nrd::Format::RG32_UINT: return VK_FORMAT_R32G32_UINT;
    case nrd::Format::RG32_SFLOAT: return VK_FORMAT_R32G32_SFLOAT;
    case nrd::Format::RGBA32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case nrd::Format::R10_G10_B10_A2_UNORM: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case nrd::Format::R11_G11_B10_UFLOAT: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    default: return VK_FORMAT_UNDEFINED;
    }
}

void destroy()
{
    State& s = nrd_state;
    if (s.instance && vk.device)
        vkDeviceWaitIdle(vk.device); /* recorded frames may still use the pipelines */
    for (Pipeline& p : s.pipelines) {
        if (p.pipeline)
            vkDestroyPipeline(vk.device, p.pipeline, nullptr);
        if (p.layout)
            vkDestroyPipelineLayout(vk.device, p.layout, nullptr);
        if (p.resources)
            vkDestroyDescriptorSetLayout(vk.device, p.resources, nullptr);
    }
    s.pipelines.clear();
    for (vk_image& i : s.permanent)
        vk_image_destroy(&i);
    for (vk_image& i : s.transient)
        vk_image_destroy(&i);
    s.permanent.clear();
    s.transient.clear();
    for (VkDescriptorPool& pool : s.pools) {
        if (pool)
            vkDestroyDescriptorPool(vk.device, pool, nullptr);
        pool = VK_NULL_HANDLE;
    }
    if (s.constants)
        vkDestroyDescriptorSetLayout(vk.device, s.constants, nullptr);
    s.constants = VK_NULL_HANDLE;
    for (unsigned i = 0; i < s.sampler_count; i++)
        vkDestroySampler(vk.device, s.samplers[i], nullptr);
    s.sampler_count = 0;
    if (s.instance)
        nrd::DestroyInstance(*s.instance);
    s.instance = nullptr;
    s.width = s.height = 0;
}

int create(int w, int h)
{
    State& s = nrd_state;
    nrd::DenoiserDesc denoiser = { 0, nrd::Denoiser::RELAX_DIFFUSE };
    nrd::InstanceCreationDesc creation = {};
    creation.denoisers = &denoiser;
    creation.denoisersNum = 1;
    if (nrd::CreateInstance(creation, s.instance) != nrd::Result::SUCCESS) {
        s.instance = nullptr;
        fprintf(stderr, "[nrd] CreateInstance failed\n");
        return 0;
    }
    const nrd::LibraryDesc& lib = *nrd::GetLibraryDesc();
    const nrd::InstanceDesc& desc = *nrd::GetInstanceDesc(*s.instance);
    s.resources_set = desc.resourcesSpaceIndex;
    s.constants_set = desc.constantBufferAndSamplersSpaceIndex;
    if (s.resources_set > 1 || s.constants_set > 1 || s.resources_set == s.constants_set) {
        fprintf(stderr, "[nrd] unexpected register spaces %u/%u\n", s.resources_set, s.constants_set);
        return 0;
    }
    s.texture_base = lib.spirvBindingOffsets.textureOffset + desc.resourcesBaseRegisterIndex;
    s.storage_base = lib.spirvBindingOffsets.storageTextureAndBufferOffset + desc.resourcesBaseRegisterIndex;
    s.constants_binding = lib.spirvBindingOffsets.constantBufferOffset + desc.constantBufferRegisterIndex;
    /* Static samplers + the constant buffer: one set layout for all pipelines. */
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (unsigned i = 0; i < desc.samplersNum && i < 8; i++) {
        VkSamplerCreateInfo si = {};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = si.minFilter = desc.samplers[i] == nrd::Sampler::LINEAR_CLAMP ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (!vk_check(vkCreateSampler(vk.device, &si, nullptr, &s.samplers[i]), "NRD sampler"))
            return 0;
        s.sampler_count = i + 1;
    }
    for (unsigned i = 0; i < s.sampler_count; i++) {
        VkDescriptorSetLayoutBinding b = {};
        b.binding = lib.spirvBindingOffsets.samplerOffset + desc.samplersBaseRegisterIndex + i;
        b.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        b.pImmutableSamplers = &s.samplers[i];
        bindings.push_back(b);
    }
    VkDescriptorSetLayoutBinding cb = {};
    cb.binding = s.constants_binding;
    cb.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cb.descriptorCount = 1;
    cb.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings.push_back(cb);
    VkDescriptorSetLayoutCreateInfo li = {};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.flags = 0;
    li.bindingCount = (uint32_t)bindings.size();
    li.pBindings = bindings.data();
    if (!vk_check(vkCreateDescriptorSetLayout(vk.device, &li, nullptr, &s.constants), "NRD constants layout"))
        return 0;
    /* Pipelines: their resources (textures, then storage textures) in set 0. */
    uint32_t most_textures = 0, most_storage = 0;
    for (unsigned i = 0; i < desc.pipelinesNum; i++) {
        const nrd::PipelineDesc& source = desc.pipelines[i];
        Pipeline p;
        bindings.clear();
        uint32_t textures = 0, storage = 0;
        for (unsigned r = 0; r < source.resourceRangesNum; r++) {
            const nrd::ResourceRangeDesc& range = source.resourceRanges[r];
            bool is_storage = range.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE;
            for (unsigned j = 0; j < range.descriptorsNum; j++) {
                VkDescriptorSetLayoutBinding b = {};
                b.binding = is_storage ? s.storage_base + storage++ : s.texture_base + textures++;
                b.descriptorType = is_storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                b.descriptorCount = 1;
                b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                bindings.push_back(b);
            }
        }
        most_textures = textures > most_textures ? textures : most_textures;
        most_storage = storage > most_storage ? storage : most_storage;
        p.bindings = (uint32_t)bindings.size();
        li.flags = 0;
        li.bindingCount = (uint32_t)bindings.size();
        li.pBindings = bindings.data();
        if (!vk_check(vkCreateDescriptorSetLayout(vk.device, &li, nullptr, &p.resources), "NRD resources layout"))
            return 0;
        VkDescriptorSetLayout sets[2];
        sets[s.resources_set] = p.resources;
        sets[s.constants_set] = s.constants;
        VkPipelineLayoutCreateInfo pli = {};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 2;
        pli.pSetLayouts = sets;
        if (!vk_check(vkCreatePipelineLayout(vk.device, &pli, nullptr, &p.layout), "NRD pipeline layout"))
            return 0;
        VkShaderModule module = vk_shader((const uint32_t*)source.computeShaderSPIRV.bytecode,
            (size_t)source.computeShaderSPIRV.size);
        if (!module)
            return 0;
        VkComputePipelineCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        ci.layout = p.layout;
        ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        ci.stage.module = module;
        ci.stage.pName = desc.shaderEntryPoint;
        uint64_t start = SDL_GetTicksNS();
        VkResult result = vkCreateComputePipelines(vk.device, vk.pipeline_cache, 1, &ci, nullptr, &p.pipeline);
        vk_pipeline_cache_note(SDL_GetTicksNS() - start);
        vkDestroyShaderModule(vk.device, module, nullptr);
        s.pipelines.push_back(p);
        if (!vk_check(result, "NRD pipeline"))
            return 0;
    }
    /* Its texture pools (zeroed: vk_image_create clears transfer targets). */
    VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    for (int pool = 0; pool < 2; pool++) {
        const nrd::TextureDesc* list = pool ? desc.transientPool : desc.permanentPool;
        unsigned count = pool ? desc.transientPoolSize : desc.permanentPoolSize;
        for (unsigned i = 0; i < count; i++) {
            VkFormat format = vk_format(list[i].format);
            int f = list[i].downsampleFactor ? list[i].downsampleFactor : 1;
            vk_image image;
            if (format == VK_FORMAT_UNDEFINED || !vk_image_create(&image, (w + f - 1) / f, (h + f - 1) / f, format, usage)) {
                fprintf(stderr, "[nrd] pool texture %u (format %u) unavailable\n", i, (unsigned)list[i].format);
                return 0;
            }
            (pool ? s.transient : s.permanent).push_back(image);
        }
    }
    /* Descriptor sets: a pool per frame slot (two sets per dispatch; 12
     * dispatches a frame, 23 on a reset). */
    uint32_t sets = 256;
    VkDescriptorPoolSize sizes[4] = { { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, sets * (most_textures ? most_textures : 1) },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, sets * (most_storage ? most_storage : 1) },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, sets }, { VK_DESCRIPTOR_TYPE_SAMPLER, sets * 8 } };
    VkDescriptorPoolCreateInfo pi = {};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = sets;
    pi.poolSizeCount = 4;
    pi.pPoolSizes = sizes;
    for (VkDescriptorPool& pool : s.pools)
        if (!vk_check(vkCreateDescriptorPool(vk.device, &pi, nullptr, &pool), "NRD descriptor pool"))
            return 0;
    for (uint64_t& serial : s.pool_serial)
        serial = ~0ull;
    s.width = w, s.height = h;
    fprintf(stderr, "[nrd] NRD %u.%u.%u RELAX diffuse ready: %dx%d, %zu pipelines, %zu+%zu pool textures\n",
        lib.versionMajor, lib.versionMinor, lib.versionBuild, w, h, s.pipelines.size(), s.permanent.size(),
        s.transient.size());
    return 1;
}
} // namespace

extern "C" int vk_nrd_built(void) { return 1; }
extern "C" int vk_nrd_available(void) { return pc_vk_active && vk.device && !nrd_state.failed; }
extern "C" const char* vk_nrd_version(void)
{
    if (!nrd_state.version[0]) {
        const nrd::LibraryDesc& lib = *nrd::GetLibraryDesc();
        snprintf(nrd_state.version, sizeof nrd_state.version, "NRD %u.%u.%u", lib.versionMajor, lib.versionMinor,
            lib.versionBuild);
    }
    return nrd_state.version;
}
extern "C" void vk_nrd_shutdown(void) { destroy(); }

extern "C" int vk_nrd_denoise(const vk_nrd_frame* f)
{
    State& s = nrd_state;
    if (s.failed || !vk_nrd_available())
        return 0;
    if (!s.instance || s.width != f->width || s.height != f->height) {
        destroy();
        if (!create(f->width, f->height)) {
            destroy();
            s.failed = 1;
            fprintf(stderr, "[nrd] unavailable: the GI keeps its own filter\n");
            return 0;
        }
    }
    nrd::CommonSettings common = {};
    memcpy(common.worldToViewMatrix, f->world_to_view, 64);
    memcpy(common.viewToClipMatrix, f->view_to_clip, 64);
    memcpy(common.worldToViewMatrixPrev, f->reset ? f->world_to_view : f->world_to_view_prev, 64);
    memcpy(common.viewToClipMatrixPrev, f->reset ? f->view_to_clip : f->view_to_clip_prev, 64);
    common.motionVectorScale[0] = common.motionVectorScale[1] = common.motionVectorScale[2] = 1.0f;
    common.isMotionVectorInWorldSpace = true;
    common.resourceSize[0] = common.resourceSizePrev[0] = common.rectSize[0] = common.rectSizePrev[0] = (uint16_t)f->width;
    common.resourceSize[1] = common.resourceSizePrev[1] = common.rectSize[1] = common.rectSizePrev[1] = (uint16_t)f->height;
    common.denoisingRange = f->denoising_range;
    /* Frame time from a monotonic clock, clamped (NRD's own timer reads the
     * wall clock; a paused game re-presents far faster than it plays). */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double ms = (now.tv_sec - s.last_time.tv_sec) * 1e3 + (now.tv_nsec - s.last_time.tv_nsec) * 1e-6;
    s.last_time = now;
    common.timeDeltaBetweenFrames = (float)(ms < 1.0 ? 1.0 : ms > 100.0 ? 100.0 : ms);
    common.frameIndex = f->frame_index;
    common.accumulationMode = f->reset ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;
    if (nrd::SetCommonSettings(*s.instance, common) != nrd::Result::SUCCESS)
        return 0;
    nrd::RelaxSettings relax = {};
    relax.enableAntiFirefly = true;
    if (f->max_frames > 0)
        relax.diffuseMaxAccumulatedFrameNum = (uint32_t)(f->max_frames < 255 ? f->max_frames : 255);
    if (f->max_fast_frames > 0)
        relax.diffuseMaxFastAccumulatedFrameNum = (uint32_t)f->max_fast_frames;
    if (relax.diffuseMaxFastAccumulatedFrameNum >= relax.diffuseMaxAccumulatedFrameNum)
        relax.diffuseMaxFastAccumulatedFrameNum = relax.diffuseMaxAccumulatedFrameNum > 1
            ? relax.diffuseMaxAccumulatedFrameNum - 1 : 0;
    if (relax.historyFixFrameNum >= relax.diffuseMaxFastAccumulatedFrameNum)
        relax.historyFixFrameNum = relax.diffuseMaxFastAccumulatedFrameNum > 0 ? relax.diffuseMaxFastAccumulatedFrameNum - 1 : 0;
    /* RELAX settings for experiments: ROOD_NRD_TUNE / RPC graphics nrd_tune
     * "key=value,..." (fast, phi, clamp, prepass, iterations, minlum,
     * antilag, hitweight, fix, firefly). */
    static int tune_read;
    if (!tune_read++ && getenv("ROOD_NRD_TUNE"))
        snprintf(pc_nrd_tune, sizeof pc_nrd_tune, "%s", getenv("ROOD_NRD_TUNE"));
    if (const char* tune = pc_nrd_tune[0] ? pc_nrd_tune : nullptr) {
        char key[32];
        float value;
        int used;
        for (const char* c = tune; sscanf(c, "%31[^=]=%f%n", key, &value, &used) == 2; c += used + (c[used] == ',')) {
            if (!strcmp(key, "fast"))
                relax.diffuseMaxFastAccumulatedFrameNum = (uint32_t)value;
            else if (!strcmp(key, "phi"))
                relax.diffusePhiLuminance = value;
            else if (!strcmp(key, "clamp"))
                relax.fastHistoryClampingSigmaScale = value;
            else if (!strcmp(key, "prepass"))
                relax.diffusePrepassBlurRadius = value;
            else if (!strcmp(key, "iterations"))
                relax.atrousIterationNum = (uint32_t)value;
            else if (!strcmp(key, "minlum"))
                relax.diffuseMinLuminanceWeight = value;
            else if (!strcmp(key, "antilag"))
                relax.antilagSettings.resetAmount = value;
            else if (!strcmp(key, "hitweight"))
                relax.minHitDistanceWeight = value;
            else if (!strcmp(key, "fix"))
                relax.historyFixFrameNum = (uint32_t)value;
            else if (!strcmp(key, "firefly"))
                relax.enableAntiFirefly = value != 0;
            if (!c[used])
                break;
        }
    }
    if (nrd::SetDenoiserSettings(*s.instance, 0, &relax) != nrd::Result::SUCCESS)
        return 0;
    const nrd::DispatchDesc* dispatches = nullptr;
    uint32_t count = 0;
    nrd::Identifier id = 0;
    if (nrd::GetComputeDispatches(*s.instance, &id, 1, dispatches, count) != nrd::Result::SUCCESS)
        return 0;
    int slot = vk.frame;
    if (s.pool_serial[slot] != vk.serial) {
        vkResetDescriptorPool(vk.device, s.pools[slot], 0);
        s.pool_serial[slot] = vk.serial;
    }
    vk_end_rendering();
    vk_barrier(); /* the inputs' writes */
    VkCommandBuffer cmd = vk_cmd();
    VkDeviceSize ubo_alignment = vk.properties.limits.minUniformBufferOffsetAlignment;
    for (uint32_t i = 0; i < count; i++) {
        const nrd::DispatchDesc& d = dispatches[i];
        const Pipeline& p = s.pipelines[d.pipelineIndex];
        VkDescriptorSet set = VK_NULL_HANDLE, constants = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo ai = {};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = s.pools[slot];
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &p.resources;
        if (!vk_check(vkAllocateDescriptorSets(vk.device, &ai, &set), "NRD descriptor set"))
            return 0;
        ai.pSetLayouts = &s.constants;
        if (!vk_check(vkAllocateDescriptorSets(vk.device, &ai, &constants), "NRD constants set"))
            return 0;
        VkDescriptorImageInfo images[32];
        VkWriteDescriptorSet writes[32];
        uint32_t textures = 0, storage = 0, n = 0;
        for (uint32_t j = 0; j < d.resourcesNum && n < 32; j++) {
            const nrd::ResourceDesc& r = d.resources[j];
            VkImageView view = VK_NULL_HANDLE;
            switch (r.type) {
            case nrd::ResourceType::IN_MV: view = f->motion; break;
            case nrd::ResourceType::IN_NORMAL_ROUGHNESS: view = f->normal_roughness; break;
            case nrd::ResourceType::IN_VIEWZ: view = f->view_z; break;
            case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST: view = f->radiance; break;
            case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST: view = f->output; break;
            case nrd::ResourceType::PERMANENT_POOL: view = s.permanent.at(r.indexInPool).view; break;
            case nrd::ResourceType::TRANSIENT_POOL: view = s.transient.at(r.indexInPool).view; break;
            default:
                fprintf(stderr, "[nrd] unexpected resource %s\n", nrd::GetResourceTypeString(r.type));
                s.failed = 1;
                return 0;
            }
            bool is_storage = r.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE;
            images[n] = { VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL };
            writes[n] = {};
            writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[n].dstSet = set;
            writes[n].dstBinding = is_storage ? s.storage_base + storage++ : s.texture_base + textures++;
            writes[n].descriptorCount = 1;
            writes[n].descriptorType = is_storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[n].pImageInfo = &images[n];
            n++;
        }
        if (n != p.bindings) {
            static int warned;
            if (!warned++)
                fprintf(stderr, "[nrd] %s: %u resources for %u bindings\n", d.name ? d.name : "?", n, p.bindings);
        }
        vkUpdateDescriptorSets(vk.device, n, writes, 0, nullptr);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, s.resources_set, 1, &set, 0, nullptr);
        uint32_t bytes = d.constantBufferDataSize ? d.constantBufferDataSize : 16;
        vk_slice cbuf = vk_arena(bytes, ubo_alignment);
        if (!cbuf.data)
            return 0;
        memset(cbuf.data, 0, bytes);
        if (d.constantBufferDataSize)
            memcpy(cbuf.data, d.constantBufferData, d.constantBufferDataSize);
        VkDescriptorBufferInfo buffer = { cbuf.buffer, cbuf.offset, bytes };
        VkWriteDescriptorSet cw = {};
        cw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cw.dstBinding = s.constants_binding;
        cw.descriptorCount = 1;
        cw.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        cw.pBufferInfo = &buffer;
        cw.dstSet = constants;
        vkUpdateDescriptorSets(vk.device, 1, &cw, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, s.constants_set, 1, &constants, 0, nullptr);
        vkCmdDispatch(cmd, d.gridWidth, d.gridHeight, 1);
        vk_barrier();
    }
    return 1;
}
#else
extern "C" int vk_nrd_built(void) { return 0; }
extern "C" int vk_nrd_available(void) { return 0; }
extern "C" int vk_nrd_denoise(const vk_nrd_frame*) { return 0; }
extern "C" const char* vk_nrd_version(void) { return ""; }
extern "C" void vk_nrd_shutdown(void) {}
#endif
