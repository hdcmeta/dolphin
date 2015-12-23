// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/LinearDiskCache.h"

#include "Core/ConfigManager.h"

#include "VideoBackends/D3D12/D3DShader.h"
#include "VideoBackends/D3D12/ShaderCache.h"

#include "VideoCommon/Statistics.h"

namespace DX12
{
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

	D3D12_SHADER_BYTECODE* last_bytecode[SHADER_STAGE_COUNT];
	ShaderGeneratorInterface* last_uid[SHADER_STAGE_COUNT];

	template<SHADER_STAGE stage>
	class ShaderCacheInserter : public LinearDiskCacheReader<ShaderGeneratorInterface, u8>
	{
	public:
		void Read(const ShaderGeneratorInterface &key, const u8* value, u32 value_size)
		{
			ShaderCache::InsertByteCode(key, stage, value, value_size);
		}
	};

	void ShaderCache::Init()
	{
		// This class intentionally shares its shader cache files with DX11, as the shaders are (right now) identical.
		// Reduces unnecessary compilation when switching between APIs.
		
		for (unsigned int i = 0; i < SHADER_STAGE_COUNT; i++)
		{
			last_bytecode[i] = nullptr;
			last_uid[i] = nullptr;
		}

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

	bool ShaderCache::LoadAndSetAsActiveShader(DSTALPHA_MODE dstAlphaMode)
	{

		return true;
	}

	bool ShaderCache::InsertByteCode(const ShaderGeneratorInterface& uid, SHADER_STAGE stage, const void* bytecode, unsigned int bytecodelen)
	{

		return true;
	}

	const D3D12_SHADER_BYTECODE* ShaderCache::GetActiveShaderBytecode(SHADER_STAGE stage)
	{
		return last_bytecode[stage];
	}

	const ShaderGeneratorInterface* ShaderCache::GetActiveShaderUid(SHADER_STAGE stage)
	{
		return last_uid[stage];
	}

	const D3D12_SHADER_BYTECODE* ShaderCache::GetShaderFromUid(SHADER_STAGE stage, ShaderGeneratorInterface* uid)
	{
		switch (stage)
		{
		case SHADER_STAGE_GEOMETRY_SHADER:
			return &gs_bytecode_cache[*reinterpret_cast<GeometryShaderUid*>(uid)];
		case SHADER_STAGE_PIXEL_SHADER:
			return &ps_bytecode_cache[*reinterpret_cast<PixelShaderUid*>(uid)];
		case SHADER_STAGE_VERTEX_SHADER:
			return &vs_bytecode_cache[*reinterpret_cast<VertexShaderUid*>(uid)];
		default:
			CHECK(0, "Invalid shader stage specified.");
			return nullptr;
		}
	}
}