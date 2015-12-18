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
#include "VideoBackends/D3D12/Globals.h"
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

static ID3D11VertexShader* SimpleVertexShader = nullptr;
static D3DBlob* SimpleVertexShaderBlob = {};
static ID3D11VertexShader* ClearVertexShader = nullptr;
static D3DBlob* ClearVertexShaderBlob = {};
static ID3D11InputLayout* SimpleLayout = nullptr;
static D3D12_INPUT_LAYOUT_DESC SimpleLayout12 = {};
static ID3D11InputLayout* ClearLayout = nullptr;
static D3D12_INPUT_LAYOUT_DESC ClearLayout12 = {};

LinearDiskCache<VertexShaderUid, u8> g_vs_disk_cache;

#ifdef USE_D3D11
ID3D11VertexShader* VertexShaderCache::GetSimpleVertexShader() { return SimpleVertexShader; }
ID3D11VertexShader* VertexShaderCache::GetClearVertexShader() { return ClearVertexShader; }
ID3D11InputLayout* VertexShaderCache::GetSimpleInputLayout() { return SimpleLayout; }
ID3D11InputLayout* VertexShaderCache::GetClearInputLayout() { return ClearLayout; }
#endif
D3D12_SHADER_BYTECODE VertexShaderCache::GetSimpleVertexShader12()
{
	D3D12_SHADER_BYTECODE shader = {};
	shader.BytecodeLength = SimpleVertexShaderBlob->Size();
	shader.pShaderBytecode = SimpleVertexShaderBlob->Data();

	return shader;
}
D3D12_SHADER_BYTECODE VertexShaderCache::GetClearVertexShader12() 
{
	D3D12_SHADER_BYTECODE shader = {};
	shader.BytecodeLength = ClearVertexShaderBlob->Size();
	shader.pShaderBytecode = ClearVertexShaderBlob->Data();

	return shader;
}
D3D12_INPUT_LAYOUT_DESC VertexShaderCache::GetSimpleInputLayout12() { return SimpleLayout12; }
D3D12_INPUT_LAYOUT_DESC VertexShaderCache::GetClearInputLayout12() { return ClearLayout12; }

#ifdef USE_D3D11
ID3D11Buffer* vscbuf = nullptr;
#endif

ID3D12Resource* vscbuf12 = nullptr;
D3D12_GPU_VIRTUAL_ADDRESS vscbuf12GPUVA = {};

void* vscbuf12data = nullptr;
const UINT vscbuf12paddedSize = (sizeof(VertexShaderConstants) + 0xff) & ~0xff;

#define vscbuf12Slots 10000
UINT currentVscbuf12 = 0; // 0 - vscbuf12Slots;

void VertexShaderCache::GetConstantBuffer12()
{
	if (VertexShaderManager::dirty)
	{
		// Overflow handled in D3DCommandListManager.
		currentVscbuf12 += vscbuf12paddedSize;

		memcpy((u8*)vscbuf12data + currentVscbuf12, &VertexShaderManager::constants, sizeof(VertexShaderConstants));

		VertexShaderManager::dirty = false;

		ADDSTAT(stats.thisFrame.bytesUniformStreamed, sizeof(VertexShaderConstants));

		D3D::currentCommandList->SetGraphicsRootConstantBufferView(
			DESCRIPTOR_TABLE_VS_CBV,
			vscbuf12GPUVA + currentVscbuf12
			);

		if (g_ActiveConfig.bEnablePixelLighting)
			D3D::currentCommandList->SetGraphicsRootConstantBufferView(
			DESCRIPTOR_TABLE_PS_CBVTWO,
			vscbuf12GPUVA + currentVscbuf12
			);

		D3D::commandListMgr->dirtyVSCBV = false;
	}
	else if (D3D::commandListMgr->dirtyVSCBV)
	{
		D3D::currentCommandList->SetGraphicsRootConstantBufferView(
			DESCRIPTOR_TABLE_VS_CBV,
			vscbuf12GPUVA + currentVscbuf12
			);

		if (g_ActiveConfig.bEnablePixelLighting)
			D3D::currentCommandList->SetGraphicsRootConstantBufferView(
				DESCRIPTOR_TABLE_PS_CBVTWO,
				vscbuf12GPUVA + currentVscbuf12
				);

		D3D::commandListMgr->dirtyVSCBV = false;
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

const char simple_shader_code[] = {
	"struct VSOUTPUT\n"
	"{\n"
	"float4 vPosition : POSITION;\n"
	"float3 vTexCoord : TEXCOORD0;\n"
	"float  vTexCoord1 : TEXCOORD1;\n"
	"};\n"
	"VSOUTPUT main(float4 inPosition : POSITION,float4 inTEX0 : TEXCOORD0)\n"
	"{\n"
	"VSOUTPUT OUT;\n"
	"OUT.vPosition = inPosition;\n"
	"OUT.vTexCoord = inTEX0.xyz;\n"
	"OUT.vTexCoord1 = inTEX0.w;\n"
	"return OUT;\n"
	"}\n"
};

const char clear_shader_code[] = {
	"struct VSOUTPUT\n"
	"{\n"
	"float4 vPosition   : POSITION;\n"
	"float4 vColor0   : COLOR0;\n"
	"};\n"
	"VSOUTPUT main(float4 inPosition : POSITION,float4 inColor0: COLOR0)\n"
	"{\n"
	"VSOUTPUT OUT;\n"
	"OUT.vPosition = inPosition;\n"
	"OUT.vColor0 = inColor0;\n"
	"return OUT;\n"
	"}\n"
};

void VertexShaderCache::Init()
{
	const D3D12_INPUT_ELEMENT_DESC simpleelems[2] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

	};

	SimpleLayout12.NumElements = ARRAYSIZE(simpleelems);
	SimpleLayout12.pInputElementDescs = new D3D12_INPUT_ELEMENT_DESC[ARRAYSIZE(simpleelems)];
	memcpy((void*)SimpleLayout12.pInputElementDescs, simpleelems, sizeof(simpleelems));

	const D3D12_INPUT_ELEMENT_DESC clearelems[2] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	ClearLayout12.NumElements = ARRAYSIZE(clearelems);
	ClearLayout12.pInputElementDescs = new D3D12_INPUT_ELEMENT_DESC[ARRAYSIZE(clearelems)];
	memcpy((void*)ClearLayout12.pInputElementDescs, clearelems, sizeof(clearelems));

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

	D3D::CompileVertexShader(simple_shader_code, &SimpleVertexShaderBlob);
	D3D::CompileVertexShader(clear_shader_code, &ClearVertexShaderBlob);

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
	D3D::commandListMgr->DestroyResourceAfterCurrentCommandListExecuted(vscbuf12);

	SAFE_RELEASE(SimpleVertexShaderBlob);
	SAFE_RELEASE(ClearVertexShaderBlob);
	SAFE_DELETE(SimpleLayout12.pInputElementDescs);
	SAFE_DELETE(ClearLayout12.pInputElementDescs);

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
	D3D::commandListMgr->dirtyPso = true;

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
