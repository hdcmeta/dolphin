// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <string>

#include "Common/FileUtil.h"
#include "Common/LinearDiskCache.h"
#include "Common/StringUtil.h"

#include "Core/ConfigManager.h"

#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DCommandListManager.h"
#include "VideoBackends/D3D12/D3DShader.h"
#include "VideoBackends/D3D12/FramebufferManager.h"
#include "VideoBackends/D3D12/GeometryShaderCache.h"

#include "VideoCommon/Debugger.h"
#include "VideoCommon/GeometryShaderGen.h"
#include "VideoCommon/GeometryShaderManager.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VideoConfig.h"

namespace DX12
{

ID3D12Resource* gscbuf12 = nullptr;
void* gscbuf12data = nullptr;
static const UINT gscbuf12paddedSize = (sizeof(GeometryShaderConstants) + 0xff) & ~0xff;

#define gscbuf12Slots 5000
unsigned int currentGscbuf12 = 0; // 0 - gscbuf12Slots;

void GeometryShaderCache::GetConstantBuffer12()
{
	if (GeometryShaderManager::dirty)
	{
		currentGscbuf12 = (currentGscbuf12 + 1) % gscbuf12Slots;

		memcpy((u8*)gscbuf12data + gscbuf12paddedSize * currentGscbuf12, &GeometryShaderManager::constants, sizeof(GeometryShaderConstants));
		
		GeometryShaderManager::dirty = false;
		
		ADDSTAT(stats.thisFrame.bytesUniformStreamed, sizeof(GeometryShaderConstants));

		D3D::command_list_mgr->m_dirty_gs_cbv = true;
	}

	if (D3D::command_list_mgr->m_dirty_gs_cbv)
	{
		D3D::current_command_list->SetGraphicsRootConstantBufferView(
			DESCRIPTOR_TABLE_GS_CBV,
			gscbuf12->GetGPUVirtualAddress() + gscbuf12paddedSize * currentGscbuf12
			);

		D3D::command_list_mgr->m_dirty_gs_cbv = false;
	}
}



void GeometryShaderCache::Init()
{
	unsigned int gscbuf12sizeInBytes = gscbuf12paddedSize * gscbuf12Slots;

	CheckHR(
		D3D::device12->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(gscbuf12sizeInBytes),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&gscbuf12)
		)
		);

	D3D::SetDebugObjectName12(gscbuf12, "vertex shader constant buffer used to emulate the GX pipeline");

	// Obtain persistent CPU pointer to GS Constant Buffer
	CheckHR(gscbuf12->Map(0, nullptr, &gscbuf12data));
}


void GeometryShaderCache::Shutdown()
{
	D3D::command_list_mgr->DestroyResourceAfterCurrentCommandListExecuted(gscbuf12);
}

}  // DX12
