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

GeometryShaderCache::GSCache GeometryShaderCache::GeometryShaders;
const GeometryShaderCache::GSCacheEntry* GeometryShaderCache::last_entry;
GeometryShaderUid GeometryShaderCache::last_uid = {};
UidChecker<GeometryShaderUid,ShaderCode> GeometryShaderCache::geometry_uid_checker;
const GeometryShaderCache::GSCacheEntry GeometryShaderCache::pass_entry;

D3D12_PRIMITIVE_TOPOLOGY_TYPE currentPrimitiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

D3DBlob* ClearGeometryShaderBlob = nullptr;
D3DBlob* CopyGeometryShaderBlob = nullptr;

LinearDiskCache<GeometryShaderUid, u8> g_gs_disk_cache;

D3D12_SHADER_BYTECODE GeometryShaderCache::GetClearGeometryShader12()
{
	D3D12_SHADER_BYTECODE bytecode = {};
	if (g_ActiveConfig.iStereoMode > 0)
	{
		bytecode.BytecodeLength = ClearGeometryShaderBlob->Size();
		bytecode.pShaderBytecode = ClearGeometryShaderBlob->Data();
	}

	return bytecode;
}

D3D12_SHADER_BYTECODE GeometryShaderCache::GetCopyGeometryShader12()
{
	D3D12_SHADER_BYTECODE bytecode = {};
	if (g_ActiveConfig.iStereoMode > 0)
	{
		bytecode.BytecodeLength = CopyGeometryShaderBlob->Size();
		bytecode.pShaderBytecode = CopyGeometryShaderBlob->Data();
	}

	return bytecode;
}

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

D3D12_PRIMITIVE_TOPOLOGY_TYPE GeometryShaderCache::GetCurrentPrimitiveTopology()
{
	return currentPrimitiveTopology;
}

// this class will load the precompiled shaders into our cache
class GeometryShaderCacheInserter : public LinearDiskCacheReader<GeometryShaderUid, u8>
{
public:
	void Read(const GeometryShaderUid &key, const u8* value, u32 value_size)
	{
		GeometryShaderCache::InsertByteCode(key, value, value_size);
	}
};

const char clear_shader_code[] = {
	"struct VSOUTPUT\n"
	"{\n"
	"	float4 vPosition   : POSITION;\n"
	"	float4 vColor0   : COLOR0;\n"
	"};\n"
	"struct GSOUTPUT\n"
	"{\n"
	"	float4 vPosition   : POSITION;\n"
	"	float4 vColor0   : COLOR0;\n"
	"	uint slice    : SV_RenderTargetArrayIndex;\n"
	"};\n"
	"[maxvertexcount(6)]\n"
	"void main(triangle VSOUTPUT o[3], inout TriangleStream<GSOUTPUT> Output)\n"
	"{\n"
	"for(int slice = 0; slice < 2; slice++)\n"
	"{\n"
	"	for(int i = 0; i < 3; i++)\n"
	"	{\n"
	"		GSOUTPUT OUT;\n"
	"		OUT.vPosition = o[i].vPosition;\n"
	"		OUT.vColor0 = o[i].vColor0;\n"
	"		OUT.slice = slice;\n"
	"		Output.Append(OUT);\n"
	"	}\n"
	"	Output.RestartStrip();\n"
	"}\n"
	"}\n"
};

const char copy_shader_code[] = {
	"struct VSOUTPUT\n"
	"{\n"
	"	float4 vPosition : POSITION;\n"
	"	float3 vTexCoord : TEXCOORD0;\n"
	"	float  vTexCoord1 : TEXCOORD1;\n"
	"};\n"
	"struct GSOUTPUT\n"
	"{\n"
	"	float4 vPosition : POSITION;\n"
	"	float3 vTexCoord : TEXCOORD0;\n"
	"	float  vTexCoord1 : TEXCOORD1;\n"
	"	uint slice    : SV_RenderTargetArrayIndex;\n"
	"};\n"
	"[maxvertexcount(6)]\n"
	"void main(triangle VSOUTPUT o[3], inout TriangleStream<GSOUTPUT> Output)\n"
	"{\n"
	"for(int slice = 0; slice < 2; slice++)\n"
	"{\n"
	"	for(int i = 0; i < 3; i++)\n"
	"	{\n"
	"		GSOUTPUT OUT;\n"
	"		OUT.vPosition = o[i].vPosition;\n"
	"		OUT.vTexCoord = o[i].vTexCoord;\n"
	"		OUT.vTexCoord.z = slice;\n"
	"		OUT.vTexCoord1 = o[i].vTexCoord1;\n"
	"		OUT.slice = slice;\n"
	"		Output.Append(OUT);\n"
	"	}\n"
	"	Output.RestartStrip();\n"
	"}\n"
	"}\n"
};

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

	// used when drawing clear quads
	D3D::CompileGeometryShader(clear_shader_code, &ClearGeometryShaderBlob);

	// used for buffer copy
	D3D::CompileGeometryShader(copy_shader_code, &CopyGeometryShaderBlob);

	Clear();

	if (!File::Exists(File::GetUserPath(D_SHADERCACHE_IDX)))
		File::CreateDir(File::GetUserPath(D_SHADERCACHE_IDX));

	// Intentionally share the same cache as DX11, as the shaders are identical. Reduces recompilation when switching APIs.
	std::string cache_filename = StringFromFormat("%sdx11-%s-gs.cache", File::GetUserPath(D_SHADERCACHE_IDX).c_str(),
			SConfig::GetInstance().m_strUniqueID.c_str());
	GeometryShaderCacheInserter inserter;
	g_gs_disk_cache.OpenAndRead(cache_filename, inserter);

	if (g_Config.bEnableShaderDebugging)
		Clear();

	last_entry = nullptr;
	last_uid = {};
}

// ONLY to be used during shutdown.
void GeometryShaderCache::Clear()
{
	for (auto& iter : GeometryShaders)
		iter.second.Destroy();
	GeometryShaders.clear();
	geometry_uid_checker.Invalidate();

	last_entry = nullptr;
}

void GeometryShaderCache::Shutdown()
{
	D3D::command_list_mgr->DestroyResourceAfterCurrentCommandListExecuted(gscbuf12);
	ClearGeometryShaderBlob->Release();
	CopyGeometryShaderBlob->Release();

	Clear();
	g_gs_disk_cache.Sync();
	g_gs_disk_cache.Close();
}

bool GeometryShaderCache::SetShader(u32 primitive_type)
{
	switch (primitive_type)
	{
	case PRIMITIVE_TRIANGLES:
		currentPrimitiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		break;
	case PRIMITIVE_LINES:
		currentPrimitiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		break;
	case PRIMITIVE_POINTS:
		currentPrimitiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		break;
	default:
		CHECK(0, "Invalid primitive type.");
		break;
	}

	GeometryShaderUid uid = GetGeometryShaderUid(primitive_type, API_D3D);

	// Check if the shader is already set
	if (uid == last_uid)
	{
		GFX_DEBUGGER_PAUSE_AT(NEXT_PIXEL_SHADER_CHANGE,true);
		return true;
	}

	last_uid = uid;
	D3D::command_list_mgr->m_dirty_pso = true;

	if (g_ActiveConfig.bEnableShaderDebugging)
	{
		ShaderCode code = GenerateGeometryShaderCode(primitive_type, API_D3D);
		geometry_uid_checker.AddToIndexAndCheck(code, uid, "Geometry", "g");
	}

	// Check if the shader is a pass-through shader
	if (uid.GetUidData()->IsPassthrough())
	{
		// Return the default pass-through shader
		last_entry = &pass_entry;
		return true;
	}

	// Check if the shader is already in the cache
	GSCache::iterator iter;
	iter = GeometryShaders.find(uid);
	if (iter != GeometryShaders.end())
	{
		const GSCacheEntry &entry = iter->second;
		last_entry = &entry;

		return (entry.shader12.pShaderBytecode != nullptr);
	}

	// Need to compile a new shader
	ShaderCode code = GenerateGeometryShaderCode(primitive_type, API_D3D);

	D3DBlob* pbytecode;
	if (!D3D::CompileGeometryShader(code.GetBuffer(), &pbytecode))
	{
		GFX_DEBUGGER_PAUSE_AT(NEXT_ERROR, true);
		return false;
	}

	// Insert the bytecode into the caches
	g_gs_disk_cache.Append(uid, pbytecode->Data(), pbytecode->Size());

	bool success = InsertByteCode(uid, pbytecode->Data(), pbytecode->Size());
	pbytecode->Release();

	if (g_ActiveConfig.bEnableShaderDebugging && success)
	{
		GeometryShaders[uid].code = code.GetBuffer();
	}

	return success;
}

bool GeometryShaderCache::InsertByteCode(const GeometryShaderUid &uid, const void* bytecode, unsigned int bytecodelen)
{
	// Make an entry in the table
	GSCacheEntry newentry;

	// In D3D12, shader bytecode is needed at Pipeline State creation time, so make a copy (as LinearDiskCache frees original after load).
	newentry.shader12.BytecodeLength = bytecodelen;
	newentry.shader12.pShaderBytecode = new u8[bytecodelen];
	memcpy(const_cast<void*>(newentry.shader12.pShaderBytecode), bytecode, bytecodelen);

	GeometryShaders[uid] = newentry;
	last_entry = &GeometryShaders[uid];

	return true;
}

}  // DX12
