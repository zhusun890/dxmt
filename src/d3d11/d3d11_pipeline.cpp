#include "d3d11_private.h"
#include "d3d11_pipeline.hpp"
#include "com/com_object.hpp"
#include "d3d11_device.hpp"
#include "d3d11_shader.hpp"
#include "log/log.hpp"
#include "objc_pointer.hpp"
#include <atomic>
#include <cassert>

namespace dxmt {

class MTLCompiledGraphicsPipeline
    : public ComObject<IMTLCompiledGraphicsPipeline> {
public:
  MTLCompiledGraphicsPipeline(IMTLD3D11Device *pDevice,
                              MTL_GRAPHICS_PIPELINE_DESC *pDesc)
      : ComObject<IMTLCompiledGraphicsPipeline>(),
        num_rtvs(pDesc->NumColorAttachments),
        depth_stencil_format(pDesc->DepthStencilFormat), device_(pDevice),
        pInputLayout(pDesc->InputLayout), pBlendState(pDesc->BlendState),
        RasterizationEnabled(pDesc->RasterizationEnabled) {
    for (unsigned i = 0; i < num_rtvs; i++) {
      rtv_formats[i] = pDesc->ColorAttachmentFormats[i];
    }
#ifndef DXMT_SHADER_VERTEX_PULLING
    if (pDesc->InputLayout && pDesc->InputLayout->NeedsFixup()) {
      MTL_SHADER_INPUT_LAYOUT_FIXUP fixup;
      pDesc->InputLayout->GetShaderFixupInfo(&fixup);
      pDesc->VertexShader->GetCompiledShaderWithInputLayoutFixup(
          fixup.sign_mask, &VertexShader);
    } else {
      pDesc->VertexShader->GetCompiledShader(&VertexShader);
    }
#else
    if (pDesc->InputLayout) {
      pDesc->VertexShader->GetCompiledVertexShaderWithVertexPulling(
          pDesc->InputLayout, &VertexShader);
    } else {
      pDesc->VertexShader->GetCompiledShader(&VertexShader);
    }
#endif
    if (pDesc->PixelShader) {
      if (pDesc->SampleMask != D3D11_DEFAULT_SAMPLE_MASK) {
        pDesc->PixelShader->GetCompiledPixelShaderWithSampleMask(
            pDesc->SampleMask, &PixelShader);
      } else {
        pDesc->PixelShader->GetCompiledShader(&PixelShader);
      }
    }
  }

  void SubmitWork() { device_->SubmitThreadgroupWork(this, &work_state_); }

  HRESULT QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMTLThreadpoolWork) ||
        riid == __uuidof(IMTLCompiledGraphicsPipeline)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  bool IsReady() final { return ready_.load(std::memory_order_relaxed); }

  void GetPipeline(MTL_COMPILED_GRAPHICS_PIPELINE *pPipeline) final {
    ready_.wait(false, std::memory_order_acquire);
    *pPipeline = {state_.ptr()};
  }

  void RunThreadpoolWork() {
    D3D11_ASSERT(!ready_ && "?wtf"); // TODO: should use a lock?

    TRACE("Start compiling 1 PSO");

    Obj<NS::Error> err;
    MTL_COMPILED_SHADER vs, ps;

    auto pipelineDescriptor =
        transfer(MTL::RenderPipelineDescriptor::alloc()->init());
    VertexShader->GetShader(&vs); // may block
    pipelineDescriptor->setVertexFunction(vs.Function);
    if (PixelShader) {
      PixelShader->GetShader(&ps); // may block
      pipelineDescriptor->setFragmentFunction(ps.Function);
    }
    pipelineDescriptor->setRasterizationEnabled(RasterizationEnabled);
#ifndef DXMT_SHADER_VERTEX_PULLING
    if (pInputLayout) {
      pInputLayout->Bind(pipelineDescriptor);
    }
#endif
    if (pBlendState) {
      pBlendState->SetupMetalPipelineDescriptor(pipelineDescriptor);
    }

    for (unsigned i = 0; i < num_rtvs; i++) {
      if (rtv_formats[i] == MTL::PixelFormatInvalid)
        continue;
      pipelineDescriptor->colorAttachments()->object(i)->setPixelFormat(
          rtv_formats[i]);
    }

    if (depth_stencil_format != MTL::PixelFormatInvalid) {
      pipelineDescriptor->setDepthAttachmentPixelFormat(depth_stencil_format);
    }
    // FIXME: don't hardcoding!
    if (depth_stencil_format == MTL::PixelFormatDepth32Float_Stencil8 ||
        depth_stencil_format == MTL::PixelFormatDepth24Unorm_Stencil8 ||
        depth_stencil_format == MTL::PixelFormatStencil8) {
      pipelineDescriptor->setStencilAttachmentPixelFormat(depth_stencil_format);
    }

    state_ = transfer(device_->GetMTLDevice()->newRenderPipelineState(
        pipelineDescriptor, &err));

    if (state_ == nullptr) {
      ERR("Failed to create PSO: ", err->localizedDescription()->utf8String());
      return; // ready_?
    }

    TRACE("Compiled 1 PSO");

    ready_.store(true);
    ready_.notify_all();
  }

private:
  UINT num_rtvs;
  MTL::PixelFormat rtv_formats[8]; // 8?
  MTL::PixelFormat depth_stencil_format;
  IMTLD3D11Device *device_;
  std::atomic_bool ready_;
  THREADGROUP_WORK_STATE work_state_;
  Com<IMTLCompiledShader> VertexShader;
  Com<IMTLCompiledShader> PixelShader;
  Com<IMTLD3D11InputLayout> pInputLayout;
  IMTLD3D11BlendState *pBlendState;
  Obj<MTL::RenderPipelineState> state_;
  bool RasterizationEnabled;
};

Com<IMTLCompiledGraphicsPipeline>
CreateGraphicsPipeline(IMTLD3D11Device *pDevice,
                       MTL_GRAPHICS_PIPELINE_DESC *pDesc) {
  Com<IMTLCompiledGraphicsPipeline> pipeline =
      new MTLCompiledGraphicsPipeline(pDevice, pDesc);
  pipeline->SubmitWork();
  return pipeline;
}

class MTLCompiledComputePipeline
    : public ComObject<IMTLCompiledComputePipeline> {
public:
  MTLCompiledComputePipeline(IMTLD3D11Device *pDevice,
                             IMTLCompiledShader *pComputeShader)
      : ComObject<IMTLCompiledComputePipeline>(), device_(pDevice),
        pComputeShader(pComputeShader) {}

  void SubmitWork() final {
    device_->SubmitThreadgroupWork(this, &work_state_);
  }

  HRESULT QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMTLThreadpoolWork) ||
        riid == __uuidof(IMTLCompiledComputePipeline)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  bool IsReady() final { return ready_.load(std::memory_order_relaxed); }

  void GetPipeline(MTL_COMPILED_COMPUTE_PIPELINE *pPipeline) final {
    ready_.wait(false, std::memory_order_acquire);
    *pPipeline = {state_.ptr()};
  }

  void RunThreadpoolWork() {
    D3D11_ASSERT(!ready_ && "?wtf"); // TODO: should use a lock?

    TRACE("Start compiling 1 PSO");

    Obj<NS::Error> err;
    MTL_COMPILED_SHADER cs;
    pComputeShader->GetShader(&cs); // may block

    auto desc = transfer(MTL::ComputePipelineDescriptor::alloc()->init());
    desc->setComputeFunction(cs.Function);

    state_ = transfer(device_->GetMTLDevice()->newComputePipelineState(
        desc, 0, nullptr, &err));

    if (state_ == nullptr) {
      ERR("Failed to create PSO: ", err->localizedDescription()->utf8String());
      return; // ready_?
    }

    TRACE("Compiled 1 PSO");

    ready_.store(true);
    ready_.notify_all();
  }

private:
  IMTLD3D11Device *device_;
  std::atomic_bool ready_;
  THREADGROUP_WORK_STATE work_state_;
  Com<IMTLCompiledShader> pComputeShader;
  Obj<MTL::ComputePipelineState> state_;
};

Com<IMTLCompiledComputePipeline>
CreateComputePipeline(IMTLD3D11Device *pDevice,
                      IMTLCompiledShader *pComputeShader) {
  Com<IMTLCompiledComputePipeline> pipeline =
      new MTLCompiledComputePipeline(pDevice, pComputeShader);
  pipeline->SubmitWork();
  return pipeline;
}

} // namespace dxmt