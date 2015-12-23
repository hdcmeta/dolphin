// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/LinearDiskCache.h"

#include "Core/ConfigManager.h"

#include "VideoBackends/D3D12/D3DCommandListManager.h"
#include "VideoBackends/D3D12/D3DShader.h"
#include "VideoBackends/D3D12/ShaderCache.h"

#include "VideoCommon/Debugger.h"
#include "VideoCommon/Statistics.h"

namespace DX12
{

// Primitive topology type is always triangle, unless the GS stage is used. This is consumed
// by the PSO created in Renderer::ApplyState.
D3D12_PRIMITIVE_TOPOLOGY_TYPE current_primitive_topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

typedef std::map<GeometryShaderUid, D3D12_SHADER_BYTECODE> GsBytecodeCache;
typedef std::map<PixelShaderUid, D3D12_SHADER_BYTECODE> PsBytecodeCache;
typedef std::map<VertexShaderUid, D3D12_SHADER_BYTECODE> VsBytecodeCache;
GsBytecodeCache gs_bytecode_cache;
PsBytecodeCache ps_bytecode_cache;
VsBytecodeCache vs_bytecode_cache;

// Only used for shader debugging..
typedef std::map<GeometryShaderUid, std::string> GsHlslCache;
typedef std::map<PixelShaderUid, std::string> PsHlslCache;
typedef std::map<VertexShaderUid, std::string> VsHlslCache;
GsHlslCache gs_hlsl_cache;
PsHlslCache ps_hlsl_cache;
VsHlslCache vs_hlsl_cache;

LinearDiskCache<ShaderGeneratorInterface, u8> g_gs_disk_cache;
LinearDiskCache<ShaderGeneratorInterface, u8> g_ps_disk_cache;
LinearDiskCache<ShaderGeneratorInterface, u8> g_vs_disk_cache;

UidChecker<GeometryShaderUid, ShaderCode> geometry_uid_checker;
UidChecker<PixelShaderUid, ShaderCode> pixel_uid_checker;
UidChecker<VertexShaderUid, ShaderCode> vertex_uid_checker;

D3D12_SHADER_BYTECODE last_geometry_shader_bytecode;
D3D12_SHADER_BYTECODE last_pixel_shader_bytecode;
D3D12_SHADER_BYTECODE last_vertex_shader_bytecode;
GeometryShaderUid last_geometry_shader_uid;
PixelShaderUid last_pixel_shader_uid;
VertexShaderUid last_vertex_shader_uid;

template<SHADER_STAGE stage>
class ShaderCacheInserter : public LinearDiskCacheReader<ShaderGeneratorInterface, u8>
{
public:
	void Read(const ShaderGeneratorInterface &key, const u8* value, u32 value_size)
	{
		D3DBlob* blob = new D3DBlob(value_size, value);
		ShaderCache::InsertByteCode(key, stage, blob);
	}
};

void ShaderCache::Init()
{
	// This class intentionally shares its shader cache files with DX11, as the shaders are (right now) identical.
	// Reduces unnecessary compilation when switching between APIs.
		
	last_geometry_shader_bytecode = {};
	last_pixel_shader_bytecode = {};
	last_vertex_shader_bytecode = {};
	last_geometry_shader_uid = {};
	last_pixel_shader_uid = {};
	last_vertex_shader_uid = {};

	SETSTAT(stats.numPixelShadersAlive, 0);
	SETSTAT(stats.numPixelShadersCreated, 0);
	SETSTAT(stats.numVertexShadersAlive, 0);
	SETSTAT(stats.numVertexShadersCreated, 0);

	// Ensure shader cache directory exists..
	std::string shader_cache_path = File::GetUserPath(D_SHADERCACHE_IDX);

	if (!File::Exists(shader_cache_path))
		File::CreateDir(File::GetUserPath(D_SHADERCACHE_IDX));

	std::string title_unique_id = SConfig::GetInstance().m_strUniqueID.c_str();

	std::string gs_cache_filename = StringFromFormat("%sdx11-%s-gs.cache", shader_cache_path.c_str(), title_unique_id.c_str());
	std::string ps_cache_filename = StringFromFormat("%sdx11-%s-ps.cache", shader_cache_path.c_str(), title_unique_id.c_str());
	std::string vs_cache_filename = StringFromFormat("%sdx11-%s-vs.cache", shader_cache_path.c_str(), title_unique_id.c_str());

	ShaderCacheInserter<SHADER_STAGE_GEOMETRY_SHADER> gs_inserter;
	g_gs_disk_cache.OpenAndRead(gs_cache_filename, gs_inserter);

	ShaderCacheInserter<SHADER_STAGE_PIXEL_SHADER> ps_inserter;
	g_ps_disk_cache.OpenAndRead(ps_cache_filename, ps_inserter);

	ShaderCacheInserter<SHADER_STAGE_VERTEX_SHADER> vs_inserter;
	g_vs_disk_cache.OpenAndRead(vs_cache_filename, vs_inserter);

	// Clear out disk cache when debugging shaders to ensure stale ones don't stick around..
	if (g_Config.bEnableShaderDebugging)
		Clear();
}

void ShaderCache::Clear()
{

}

void ShaderCache::Shutdown()
{
	for (auto& iter : gs_bytecode_cache)
		SAFE_DELETE(iter.second.pShaderBytecode);

	for (auto& iter : ps_bytecode_cache)
		SAFE_DELETE(iter.second.pShaderBytecode);

	for (auto& iter : vs_bytecode_cache)
		SAFE_DELETE(iter.second.pShaderBytecode);

	gs_bytecode_cache.clear();
	ps_bytecode_cache.clear();
	vs_bytecode_cache.clear();

	g_gs_disk_cache.Sync();
	g_gs_disk_cache.Close();
	g_ps_disk_cache.Sync();
	g_ps_disk_cache.Close();
	g_vs_disk_cache.Sync();
	g_vs_disk_cache.Close();

	if (g_Config.bEnableShaderDebugging)
	{
		gs_hlsl_cache.clear();
		ps_hlsl_cache.clear();
		vs_hlsl_cache.clear();
	}

	geometry_uid_checker.Invalidate();
	pixel_uid_checker.Invalidate();
	vertex_uid_checker.Invalidate();
}

void ShaderCache::LoadAndSetActiveShaders(DSTALPHA_MODE ps_dst_alpha_mode, u32 gs_primitive_type)
{
	switch (gs_primitive_type)
	{
	case PRIMITIVE_TRIANGLES:
		current_primitive_topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		break;
	case PRIMITIVE_LINES:
		current_primitive_topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		break;
	case PRIMITIVE_POINTS:
		current_primitive_topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		break;
	default:
		CHECK(0, "Invalid primitive type.");
		break;
	}

	GeometryShaderUid gs_uid = GetGeometryShaderUid(gs_primitive_type, API_D3D);
	PixelShaderUid ps_uid = GetPixelShaderUid(ps_dst_alpha_mode, API_D3D);
	VertexShaderUid vs_uid = GetVertexShaderUid(API_D3D);

	bool gs_changed = gs_uid != last_geometry_shader_uid;
	bool ps_changed = ps_uid != last_pixel_shader_uid;
	bool vs_changed = vs_uid != last_vertex_shader_uid;

	if (!gs_changed && !ps_changed && !vs_changed)
	{
		return;
	}

	// A Uid has changed, so the PSO will need to be reset at next ApplyState.
	D3D::command_list_mgr->m_dirty_pso = true;

	if (g_ActiveConfig.bEnableShaderDebugging)
	{
		ShaderCode code;
		if (gs_changed)
		{
			code = GenerateGeometryShaderCode(gs_primitive_type, API_D3D);
			geometry_uid_checker.AddToIndexAndCheck(code, gs_uid, "Geometry", "g");
		}

		if (ps_changed)
		{
			code = GeneratePixelShaderCode(ps_dst_alpha_mode, API_D3D);
			pixel_uid_checker.AddToIndexAndCheck(code, ps_uid, "Pixel", "p");
		}
		
		if (vs_changed)
		{
			code = GenerateVertexShaderCode(API_D3D);
			vertex_uid_checker.AddToIndexAndCheck(code, vs_uid, "Vertex", "v");
		}
	}

	if (gs_changed)
	{
		if (gs_uid.GetUidData()->IsPassthrough())
		{
			last_geometry_shader_bytecode = {};
		}
		else
		{
			auto gs_iterator = gs_bytecode_cache.find(gs_uid);
			if (gs_iterator != gs_bytecode_cache.end())
			{
				last_geometry_shader_bytecode = gs_iterator->second;
			}
			else
			{
				ShaderCode gs_code = GenerateGeometryShaderCode(gs_primitive_type, API_D3D);
				D3DBlob* gs_bytecode = nullptr;
				
				if (!D3D::CompileGeometryShader(gs_code.GetBuffer(), &gs_bytecode))
				{
					GFX_DEBUGGER_PAUSE_AT(NEXT_ERROR, true);
					return;
				}

				InsertByteCode(gs_uid, SHADER_STAGE_GEOMETRY_SHADER, gs_bytecode);

				if (g_ActiveConfig.bEnableShaderDebugging && gs_bytecode)
				{
					gs_hlsl_cache[gs_uid] = gs_code.GetBuffer();
				}
			}
		}
	}

	if (ps_changed)
	{
		auto ps_iterator = ps_bytecode_cache.find(ps_uid);
		if (ps_iterator != ps_bytecode_cache.end())
		{
			last_pixel_shader_bytecode = ps_iterator->second;
			GFX_DEBUGGER_PAUSE_AT(NEXT_PIXEL_SHADER_CHANGE, true);
		}
		else
		{
			ShaderCode ps_code = GeneratePixelShaderCode(ps_dst_alpha_mode, API_D3D);
			D3DBlob* ps_bytecode = nullptr;

			if (!D3D::CompilePixelShader(ps_code.GetBuffer(), &ps_bytecode))
			{
				GFX_DEBUGGER_PAUSE_AT(NEXT_ERROR, true);
				return;
			}

			InsertByteCode(ps_uid, SHADER_STAGE_PIXEL_SHADER, ps_bytecode);

			if (g_ActiveConfig.bEnableShaderDebugging && ps_bytecode)
			{
				ps_hlsl_cache[ps_uid] = ps_code.GetBuffer();
			}

			ps_bytecode->Release();
		}
	}

	if (vs_changed)
	{
		auto vs_iterator = vs_bytecode_cache.find(vs_uid);
		if (vs_iterator != vs_bytecode_cache.end())
		{
			last_vertex_shader_bytecode = vs_iterator->second;
			GFX_DEBUGGER_PAUSE_AT(NEXT_PIXEL_SHADER_CHANGE, true);
		}
		else
		{
			ShaderCode vs_code = GenerateVertexShaderCode(API_D3D);
			D3DBlob* vs_bytecode = nullptr;

			if (!D3D::CompileVertexShader(vs_code.GetBuffer(), &vs_bytecode))
			{
				GFX_DEBUGGER_PAUSE_AT(NEXT_ERROR, true);
				return;
			}

			InsertByteCode(vs_uid, SHADER_STAGE_VERTEX_SHADER, vs_bytecode);

			if (g_ActiveConfig.bEnableShaderDebugging && vs_bytecode)
			{
				vs_hlsl_cache[vs_uid] = vs_code.GetBuffer();
			}
		}
	}
}

void ShaderCache::InsertByteCode(const ShaderGeneratorInterface& uid, SHADER_STAGE stage, D3DBlob* bytecode_blob)
{
	// Note: Don't release the incoming bytecode, we need it to stick around, since in D3D12
	// the raw bytecode itself is bound. It is released at Shutdown() time.

	D3D12_SHADER_BYTECODE shader_bytecode;
	shader_bytecode.pShaderBytecode = new u8[bytecode_blob->Size()];
	memcpy(const_cast<void*>(shader_bytecode.pShaderBytecode), bytecode_blob->Data(), bytecode_blob->Size());
	shader_bytecode.BytecodeLength = bytecode_blob->Size();

	switch (stage)
	{
	case SHADER_STAGE_GEOMETRY_SHADER:
		gs_bytecode_cache[reinterpret_cast<const GeometryShaderUid&>(uid)] = shader_bytecode;
		last_geometry_shader_bytecode = shader_bytecode;
		g_gs_disk_cache.Append(uid, bytecode_blob->Data(), bytecode_blob->Size());
		break;
	case SHADER_STAGE_PIXEL_SHADER:
		ps_bytecode_cache[reinterpret_cast<const PixelShaderUid&>(uid)] = shader_bytecode;
		last_pixel_shader_bytecode = shader_bytecode;
		g_ps_disk_cache.Append(uid, bytecode_blob->Data(), bytecode_blob->Size());
		INCSTAT(stats.numPixelShadersCreated);
		SETSTAT(stats.numPixelShadersAlive, (int)ps_bytecode_cache.size());
		break;
	case SHADER_STAGE_VERTEX_SHADER:
		vs_bytecode_cache[reinterpret_cast<const VertexShaderUid&>(uid)] = shader_bytecode;
		last_vertex_shader_bytecode = shader_bytecode;
		g_vs_disk_cache.Append(uid, bytecode_blob->Data(), bytecode_blob->Size());
		INCSTAT(stats.numVertexShadersCreated);
		SETSTAT(stats.numVertexShadersAlive, (int)vs_bytecode_cache.size());
		break;
	default:
		CHECK(0, "Invalid shader stage specified.");
	}

	bytecode_blob->Release();
}

D3D12_SHADER_BYTECODE ShaderCache::GetActiveShaderBytecode(SHADER_STAGE stage)
{
	switch (stage)
	{
	case SHADER_STAGE_GEOMETRY_SHADER:
		return last_geometry_shader_bytecode;
	case SHADER_STAGE_PIXEL_SHADER:
		return last_pixel_shader_bytecode;
	case SHADER_STAGE_VERTEX_SHADER:
		return last_vertex_shader_bytecode;
	default:
		CHECK(0, "Invalid shader stage specified.");
		return D3D12_SHADER_BYTECODE();
	}
}

D3D12_PRIMITIVE_TOPOLOGY_TYPE ShaderCache::GetCurrentPrimitiveTopology()
{
	return current_primitive_topology;
}

const ShaderGeneratorInterface* ShaderCache::GetActiveShaderUid(SHADER_STAGE stage)
{
	switch (stage)
	{
	case SHADER_STAGE_GEOMETRY_SHADER:
		return &last_geometry_shader_uid;
	case SHADER_STAGE_PIXEL_SHADER:
		return &last_pixel_shader_uid;
	case SHADER_STAGE_VERTEX_SHADER:
		return &last_vertex_shader_uid;
	default:
		CHECK(0, "Invalid shader stage specified.");
		return nullptr;
	}
}

D3D12_SHADER_BYTECODE ShaderCache::GetShaderFromUid(SHADER_STAGE stage, const ShaderGeneratorInterface* uid)
{
	switch (stage)
	{
	case SHADER_STAGE_GEOMETRY_SHADER:
		return gs_bytecode_cache[*reinterpret_cast<const GeometryShaderUid*>(uid)];
	case SHADER_STAGE_PIXEL_SHADER:
		return ps_bytecode_cache[*reinterpret_cast<const PixelShaderUid*>(uid)];
	case SHADER_STAGE_VERTEX_SHADER:
		return vs_bytecode_cache[*reinterpret_cast<const VertexShaderUid*>(uid)];
	default:
		CHECK(0, "Invalid shader stage specified.");
		return D3D12_SHADER_BYTECODE();
	}
}

}