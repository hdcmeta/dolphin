// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <string>

#include "Common/FileUtil.h"
#include "Common/LinearDiskCache.h"
#include "Common/StringUtil.h"

#include "Core/ConfigManager.h"

#include "VideoBackends/D3D12/D3DCommandListManager.h"
#include "VideoBackends/D3D12/D3DShader.h"
#include "VideoBackends/D3D12/D3DUtil.h"
#include "VideoBackends/D3D12/VertexShaderCache.h"

#include "VideoCommon/Debugger.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VertexShaderGen.h"
#include "VideoCommon/VertexShaderManager.h"

namespace DX12 {

VertexShaderCache::VSCache VertexShaderCache::vshaders;
const VertexShaderCache::VSCacheEntry *VertexShaderCache::last_entry;
VertexShaderUid VertexShaderCache::last_uid = {};
UidChecker<VertexShaderUid,ShaderCode> VertexShaderCache::vertex_uid_checker;

LinearDiskCache<VertexShaderUid, u8> g_vs_disk_cache;

ID3D12Resource* vscbuf12 = nullptr;
D3D12_GPU_VIRTUAL_ADDRESS vscbuf12GPUVA = {};

void* vscbuf12data = nullptr;
const UINT vscbuf12paddedSize = (sizeof(VertexShaderConstants) + 0xff) & ~0xff;

#define vscbuf12Slots 10000
UINT current_vsc_buf12 = 0; // 0 - vscbuf12Slots;

void VertexShaderCache::GetConstantBuffer12()
{
	if (VertexShaderManager::dirty)
	{
		// Overflow handled in D3DCommandListManager.
		current_vsc_buf12 += vscbuf12paddedSize;

		memcpy((u8*)vscbuf12data + current_vsc_buf12, &VertexShaderManager::constants, sizeof(VertexShaderConstants));

		VertexShaderManager::dirty = false;

		ADDSTAT(stats.thisFrame.bytesUniformStreamed, sizeof(VertexShaderConstants));

		D3D::current_command_list->SetGraphicsRootConstantBufferView(
			DESCRIPTOR_TABLE_VS_CBV,
			vscbuf12GPUVA + current_vsc_buf12
			);

		if (g_ActiveConfig.bEnablePixelLighting)
			D3D::current_command_list->SetGraphicsRootConstantBufferView(
			DESCRIPTOR_TABLE_PS_CBVTWO,
			vscbuf12GPUVA + current_vsc_buf12
			);

		D3D::command_list_mgr->m_dirty_vs_cbv = false;
	}
	else if (D3D::command_list_mgr->m_dirty_vs_cbv)
	{
		D3D::current_command_list->SetGraphicsRootConstantBufferView(
			DESCRIPTOR_TABLE_VS_CBV,
			vscbuf12GPUVA + current_vsc_buf12
			);

		if (g_ActiveConfig.bEnablePixelLighting)
			D3D::current_command_list->SetGraphicsRootConstantBufferView(
				DESCRIPTOR_TABLE_PS_CBVTWO,
				vscbuf12GPUVA + current_vsc_buf12
				);

		D3D::command_list_mgr->m_dirty_vs_cbv = false;
	}
}

// this class will load the precompiled shaders into our cache
class VertexShaderCacheInserter : public LinearDiskCacheReader<VertexShaderUid, u8>
{
public:
	void Read(const VertexShaderUid &key, const u8* value, u32 value_size)
	{
		D3DBlob* blob = new D3DBlob(value_size, value);
		VertexShaderCache::InsertByteCode(key, blob);
	}
};

void VertexShaderCache::Init()
{
	unsigned int vscbuf12sizeInBytes = vscbuf12paddedSize * vscbuf12Slots;

	CheckHR(
		D3D::device12->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(vscbuf12sizeInBytes),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&vscbuf12)
			)
		);

	D3D::SetDebugObjectName12(vscbuf12, "vertex shader constant buffer used to emulate the GX pipeline");

	// Obtain persistent CPU pointer to PS Constant Buffer
	CheckHR(vscbuf12->Map(0, nullptr, &vscbuf12data));

	// Obtain GPU VA for buffer, used at binding time.
	vscbuf12GPUVA = vscbuf12->GetGPUVirtualAddress();

	Clear();

	if (!File::Exists(File::GetUserPath(D_SHADERCACHE_IDX)))
		File::CreateDir(File::GetUserPath(D_SHADERCACHE_IDX));

	SETSTAT(stats.numVertexShadersCreated, 0);
	SETSTAT(stats.numVertexShadersAlive, 0);

	// Intentionally share the same cache as DX11, as the shaders are identical. Reduces recompilation when switching APIs.
	std::string cache_filename = StringFromFormat("%sdx11-%s-vs.cache", File::GetUserPath(D_SHADERCACHE_IDX).c_str(),
			SConfig::GetInstance().m_strUniqueID.c_str());
	VertexShaderCacheInserter inserter;
	g_vs_disk_cache.OpenAndRead(cache_filename, inserter);

	if (g_Config.bEnableShaderDebugging)
		Clear();

	last_entry = nullptr;
	last_uid = {};
}

void VertexShaderCache::Clear()
{
	for (auto& iter : vshaders)
		iter.second.Destroy();
	vshaders.clear();
	vertex_uid_checker.Invalidate();

	last_entry = nullptr;
}

void VertexShaderCache::Shutdown()
{
	D3D::command_list_mgr->DestroyResourceAfterCurrentCommandListExecuted(vscbuf12);

	Clear();
	g_vs_disk_cache.Sync();
	g_vs_disk_cache.Close();
}

bool VertexShaderCache::SetShader()
{
	VertexShaderUid uid = GetVertexShaderUid(API_D3D);

	if (uid == last_uid)
	{
		GFX_DEBUGGER_PAUSE_AT(NEXT_VERTEX_SHADER_CHANGE, true);
		return (last_entry->shader12.pShaderBytecode != nullptr);
	}

	last_uid = uid;
	D3D::command_list_mgr->m_dirty_pso = true;

	if (g_ActiveConfig.bEnableShaderDebugging)
	{
		ShaderCode code = GenerateVertexShaderCode(API_D3D);
		vertex_uid_checker.AddToIndexAndCheck(code, uid, "Vertex", "v");
	}

	VSCache::iterator iter = vshaders.find(uid);
	if (iter != vshaders.end())
	{
		const VSCacheEntry &entry = iter->second;
		last_entry = &entry;

		GFX_DEBUGGER_PAUSE_AT(NEXT_VERTEX_SHADER_CHANGE, true);
		return (entry.shader12.pShaderBytecode != nullptr);
	}

	ShaderCode code = GenerateVertexShaderCode(API_D3D);

	D3DBlob* pbytecode = nullptr;
	D3D::CompileVertexShader(code.GetBuffer(), &pbytecode);

	if (pbytecode == nullptr)
	{
		GFX_DEBUGGER_PAUSE_AT(NEXT_ERROR, true);
		return false;
	}
	g_vs_disk_cache.Append(uid, pbytecode->Data(), pbytecode->Size());

	bool success = InsertByteCode(uid, pbytecode);
	pbytecode->Release();

	if (g_ActiveConfig.bEnableShaderDebugging && success)
	{
		vshaders[uid].code = code.GetBuffer();
	}

	GFX_DEBUGGER_PAUSE_AT(NEXT_VERTEX_SHADER_CHANGE, true);
	return success;
}

bool VertexShaderCache::InsertByteCode(const VertexShaderUid &uid, D3DBlob* bcodeblob)
{
	// Make an entry in the table
	VSCacheEntry entry;

	// In D3D12, shader bytecode is needed at Pipeline State creation time. The D3D11 path already kept shader bytecode around
	// for subsequent InputLayout creation, so just take advantage of that.

	entry.SetByteCode(bcodeblob);
	entry.shader12.BytecodeLength = bcodeblob->Size();
	entry.shader12.pShaderBytecode = bcodeblob->Data();

	vshaders[uid] = entry;
	last_entry = &vshaders[uid];

	INCSTAT(stats.numVertexShadersCreated);
	SETSTAT(stats.numVertexShadersAlive, (int)vshaders.size());

	return true;
}

}  // namespace DX12
