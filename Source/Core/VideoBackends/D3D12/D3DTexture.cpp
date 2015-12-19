// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/CommonTypes.h"
#include "Common/MsgHandler.h"
#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DCommandListManager.h"
#include "VideoBackends/D3D12/D3DDescriptorHeapManager.h"
#include "VideoBackends/D3D12/D3DTexture.h"
#include "VideoBackends/D3D12/D3DUtil.h"
#include "VideoBackends/D3D12/FramebufferManager.h"
#include "VideoBackends/D3D12/Render.h"

namespace DX12
{
	static ID3D12Resource* s_replaceRGBATexture2DUploadHeap = nullptr;
	void* s_replaceRGBATexture2DUploadHeapData = 0;
	UINT s_replaceRGBATexture2DUploadHeapOffset = 0;

	static UINT s_replaceRGBATexture2DUploadHeapSize = 8 * 1024 * 1024;

namespace D3D
{

	void CleanupPersistentD3DTextureResources()
	{
		SAFE_RELEASE(s_replaceRGBATexture2DUploadHeap);
		s_replaceRGBATexture2DUploadHeapData = 0;
		s_replaceRGBATexture2DUploadHeapOffset = 0;
	}

void ReplaceRGBATexture2D(ID3D12Resource* pTexture12, const u8* buffer, unsigned int width, unsigned int height, unsigned int src_pitch, unsigned int level, D3D12_RESOURCE_STATES currentResourceState)
{
	unsigned int uploadSize =
		D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT +
		((src_pitch + (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)) * height;

	if (s_replaceRGBATexture2DUploadHeapOffset + uploadSize > s_replaceRGBATexture2DUploadHeapSize)
	{
		// Ran out of room.. so wait for GPU queue to drain, free resource, then reallocate buffer with double the size.

		// Flush current work, then wait.
		D3D::commandListMgr->DestroyResourceAfterCurrentCommandListExecuted(s_replaceRGBATexture2DUploadHeap);
		s_replaceRGBATexture2DUploadHeap = nullptr;

		D3D::commandListMgr->ExecuteQueuedWork(true);

		// Reset state to defaults.
		g_renderer->SetViewport();
		D3D::currentCommandList->OMSetRenderTargets(1, &FramebufferManager::GetEFBColorTexture()->GetRTV12(), FALSE, &FramebufferManager::GetEFBDepthTexture()->GetDSV12());
		
		s_replaceRGBATexture2DUploadHeapSize *= 2;
		s_replaceRGBATexture2DUploadHeapOffset = 0;
	}

	if (!s_replaceRGBATexture2DUploadHeap)
	{
		CheckHR(
			device12->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(s_replaceRGBATexture2DUploadHeapSize),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&s_replaceRGBATexture2DUploadHeap)
				)
			);
	}

	D3D12_SUBRESOURCE_DATA subresourceData12 = {
		buffer,    // const void *pData;
		src_pitch, // LONG_PTR RowPitch;
		0,         // LONG_PTR SlicePitch;
	};

	ResourceBarrier(currentCommandList, pTexture12, currentResourceState, D3D12_RESOURCE_STATE_COPY_DEST, level);

	CHECK(0 != UpdateSubresources(currentCommandList, pTexture12, s_replaceRGBATexture2DUploadHeap, s_replaceRGBATexture2DUploadHeapOffset, level, 1, &subresourceData12), "UpdateSubresources failed.");

	ResourceBarrier(D3D::currentCommandList, pTexture12, D3D12_RESOURCE_STATE_COPY_DEST, currentResourceState, level);

	s_replaceRGBATexture2DUploadHeapOffset += uploadSize;

	s_replaceRGBATexture2DUploadHeapOffset = (s_replaceRGBATexture2DUploadHeapOffset + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1); // Align offset in upload heap to 512 bytes, as required.
}

}  // namespace

D3DTexture2D* D3DTexture2D::Create(unsigned int width, unsigned int height, D3D11_BIND_FLAG bind, D3D11_USAGE usage, DXGI_FORMAT fmt, unsigned int levels, unsigned int slices, D3D12_SUBRESOURCE_DATA* data)
{
	ID3D12Resource* pTexture12 = nullptr;

	D3D12_RESOURCE_DESC texdesc12 = CD3DX12_RESOURCE_DESC::Tex2D(
		fmt,
		width,
		height,
		slices,
		levels
		);

	D3D12_CLEAR_VALUE optimizedClearValue = {};
	optimizedClearValue.Format = fmt;

	if (bind & D3D11_BIND_RENDER_TARGET)
	{
		texdesc12.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		optimizedClearValue.Color[0] = 0.0f;
		optimizedClearValue.Color[1] = 0.0f;
		optimizedClearValue.Color[2] = 0.0f;
		optimizedClearValue.Color[3] = 1.0f;
	}

	if (bind & D3D11_BIND_DEPTH_STENCIL)
	{
		texdesc12.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		optimizedClearValue.DepthStencil.Depth = 0.0f;
		optimizedClearValue.DepthStencil.Stencil = 0;
	}

	CheckHR(
		D3D::device12->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC(texdesc12),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&optimizedClearValue,
			IID_PPV_ARGS(&pTexture12)
			)
		);

	D3D::SetDebugObjectName12(pTexture12, "Texture created via D3DTexture2D::Create");
	D3DTexture2D* ret = new D3DTexture2D(pTexture12, bind, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, false, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	if (data)
	{
		DX12::D3D::ReplaceRGBATexture2D(pTexture12, reinterpret_cast<const u8*>(data->pData), width, height, static_cast<unsigned int>(data->RowPitch), 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	SAFE_RELEASE(pTexture12);
	return ret;
}

void D3DTexture2D::AddRef()
{
	++ref;
}

UINT D3DTexture2D::Release()
{
	--ref;
	if (ref == 0)
	{
		delete this;
		return 0;
	}
	return ref;
}

ID3D12Resource* &D3DTexture2D::GetTex12() { return tex12; }
D3D12_CPU_DESCRIPTOR_HANDLE &D3DTexture2D::GetSRV12cpu() { return srv12cpu; }
D3D12_GPU_DESCRIPTOR_HANDLE &D3DTexture2D::GetSRV12gpu() { return srv12gpu; }
D3D12_CPU_DESCRIPTOR_HANDLE &D3DTexture2D::GetSRV12gpuCpuShadow() { return srv12gpuCpuShadow; }
D3D12_CPU_DESCRIPTOR_HANDLE &D3DTexture2D::GetDSV12() { return dsv12; }
D3D12_CPU_DESCRIPTOR_HANDLE &D3DTexture2D::GetRTV12() { return rtv12; }

D3DTexture2D::D3DTexture2D(ID3D12Resource* texptr, D3D11_BIND_FLAG bind,
							DXGI_FORMAT srv_format, DXGI_FORMAT dsv_format, DXGI_FORMAT rtv_format, bool multisampled, D3D12_RESOURCE_STATES resourceState)
							: ref(1), tex12(texptr), resourceState(resourceState), multisampled(multisampled),
							srv12cpu({}), srv12gpu({}), srv12gpuCpuShadow({}), rtv12({}), dsv12({})
{
	D3D12_SRV_DIMENSION srv_dim12 = multisampled ? D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	D3D12_DSV_DIMENSION dsv_dim12 = multisampled ? D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
	D3D12_RTV_DIMENSION rtv_dim12 = multisampled ? D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D12_RTV_DIMENSION_TEXTURE2DARRAY;

	if (bind & D3D11_BIND_SHADER_RESOURCE)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {
			srv_format, // DXGI_FORMAT Format
			srv_dim12   // D3D12_SRV_DIMENSION ViewDimension
		};

		if (srv_dim12 == D3D12_SRV_DIMENSION_TEXTURE2DARRAY)
		{
			srv_desc.Texture2DArray.MipLevels = -1;
			srv_desc.Texture2DArray.MostDetailedMip = 0;
			srv_desc.Texture2DArray.ResourceMinLODClamp = 0;
			srv_desc.Texture2DArray.ArraySize = -1;
		}
		else
		{
			srv_desc.Texture2DMSArray.ArraySize = -1;
		}

		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		CHECK(D3D::gpuDescriptorHeapMgr->Allocate(&srv12cpu, &srv12gpu, &srv12gpuCpuShadow), "Error: Ran out of permenant slots in GPU descriptor heap, but don't support rolling over heap.");

		D3D::device12->CreateShaderResourceView(tex12, &srv_desc, srv12cpu);
		D3D::device12->CreateShaderResourceView(tex12, &srv_desc, srv12gpuCpuShadow);
	}

	if (bind & D3D11_BIND_DEPTH_STENCIL)
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {
			dsv_format,          // DXGI_FORMAT Format
			dsv_dim12,           // D3D12_DSV_DIMENSION 
			D3D12_DSV_FLAG_NONE  // D3D12_DSV_FLAG Flags
		};

		if (dsv_dim12 == D3D12_DSV_DIMENSION_TEXTURE2DARRAY)
			dsv_desc.Texture2DArray.ArraySize = -1;
		else
			dsv_desc.Texture2DMSArray.ArraySize = -1;

		D3D::dsvDescriptorHeapMgr->Allocate(&dsv12);
		D3D::device12->CreateDepthStencilView(tex12, &dsv_desc, dsv12);
	}

	if (bind & D3D11_BIND_RENDER_TARGET)
	{
		D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {
			rtv_format, // DXGI_FORMAT Format
			rtv_dim12   // D3D12_RTV_DIMENSION ViewDimension
		};

		if (rtv_dim12 == D3D12_RTV_DIMENSION_TEXTURE2DARRAY)
			rtv_desc.Texture2DArray.ArraySize = -1;
		else
			rtv_desc.Texture2DMSArray.ArraySize = -1;

		D3D::rtvDescriptorHeapMgr->Allocate(&rtv12);
		D3D::device12->CreateRenderTargetView(tex12, &rtv_desc, rtv12);
	}

	tex12->AddRef();
}

void D3DTexture2D::TransitionToResourceState(ID3D12GraphicsCommandList* commandList, D3D12_RESOURCE_STATES stateAfter)
{
	DX12::D3D::ResourceBarrier(commandList, tex12, resourceState, stateAfter, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	resourceState = stateAfter;
}

D3DTexture2D::~D3DTexture2D()
{
	DX12::D3D::commandListMgr->DestroyResourceAfterCurrentCommandListExecuted(tex12);

	if (srv12cpu.ptr)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC nullSRVDesc = {};
		nullSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		nullSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

		nullSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		DX12::D3D::device12->CreateShaderResourceView(NULL, &nullSRVDesc, srv12cpu);
	}
}

}  // namespace DX12
