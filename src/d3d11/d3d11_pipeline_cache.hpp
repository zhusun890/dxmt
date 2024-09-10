#pragma once
#include "com/com_guid.hpp"
#include "d3d11_state_object.hpp"

DEFINE_COM_INTERFACE("1a59c71f-f6bc-4aa1-b97e-35732f75a4eb",
                     IMTLD3D11PipeineCache)
    : public ID3D11DeviceChild {

  virtual HRESULT AddVertexShader(const void *pBytecode,
                                  uint32_t BytecodeLength,
                                  ID3D11VertexShader **ppShader) = 0;
  virtual HRESULT AddPixelShader(const void *pBytecode, uint32_t BytecodeLength,
                                 ID3D11PixelShader **ppShader) = 0;
  virtual HRESULT AddHullShader(const void *pBytecode, uint32_t BytecodeLength,
                                ID3D11HullShader **ppShader) = 0;
  virtual HRESULT AddDomainShader(const void *pBytecode,
                                  uint32_t BytecodeLength,
                                  ID3D11DomainShader **ppShader) = 0;
  virtual HRESULT AddGeometryShader(const void *pBytecode,
                                    uint32_t BytecodeLength,
                                    ID3D11GeometryShader **ppShader) = 0;
  virtual HRESULT AddInputLayout(
      const void *pShaderBytecodeWithInputSignature,
      const D3D11_INPUT_ELEMENT_DESC *pInputElementDesc, UINT NumElements,
      ID3D11InputLayout **ppInputLayout) = 0;
  virtual HRESULT AddBlendState(const D3D11_BLEND_DESC1 *pBlendDesc,
                                IMTLD3D11BlendState **ppBlendState) = 0;
  virtual void GetGraphicsPipeline(MTL_GRAPHICS_PIPELINE_DESC * pPipelineDesc,
                                   IMTLCompiledGraphicsPipeline *
                                       *ppPipeline) = 0;
  virtual void GetTessellationPipeline(
      MTL_TESSELLATION_PIPELINE_DESC * pPiplineDesc,
      IMTLCompiledTessellationPipeline * *ppPipeline) = 0;
};

namespace dxmt {

using MTLD3D11PipelineCacheBase = ManagedDeviceChild<IMTLD3D11PipeineCache>;

std::unique_ptr<MTLD3D11PipelineCacheBase>
InitializePipelineCache(IMTLD3D11Device *device);

} // namespace dxmt
