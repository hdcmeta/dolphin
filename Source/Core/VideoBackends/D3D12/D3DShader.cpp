// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <string>

#include "Common/StringUtil.h"
#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DShader.h"
#include "VideoCommon/VideoConfig.h"

namespace DX12
{

namespace D3D
{

// code->bytecode
bool CompileVertexShader(const std::string& code, D3DBlob** blob)
{
	ID3D10Blob* shaderBuffer = nullptr;
	ID3D10Blob* errorBuffer = nullptr;

#if defined(_DEBUG) || defined(DEBUGFAST)
	UINT flags = D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY|D3D10_SHADER_DEBUG;
#else
	UINT flags = D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY|D3D10_SHADER_OPTIMIZATION_LEVEL3|D3D10_SHADER_SKIP_VALIDATION;
#endif
	HRESULT hr = PD3DCompile(code.c_str(), code.length(), nullptr, nullptr, nullptr, "main", D3D::VertexShaderVersionString(),
							flags, 0, &shaderBuffer, &errorBuffer);
	if (errorBuffer)
	{
		INFO_LOG(VIDEO, "Vertex shader compiler messages:\n%s\n",
			(const char*)errorBuffer->GetBufferPointer());
	}

	if (FAILED(hr))
	{
		static int num_failures = 0;
		std::string filename = StringFromFormat("%sbad_vs_%04i.txt", File::GetUserPath(D_DUMP_IDX).c_str(), num_failures++);
		std::ofstream file;
		OpenFStream(file, filename, std::ios_base::out);
		file << code;
		file.close();

		PanicAlert("Failed to compile vertex shader: %s\nDebug info (%s):\n%s",
			filename.c_str(), D3D::VertexShaderVersionString(), (const char*)errorBuffer->GetBufferPointer());

		*blob = nullptr;
		errorBuffer->Release();
	}
	else
	{
		*blob = new D3DBlob(shaderBuffer);
		shaderBuffer->Release();
	}
	return SUCCEEDED(hr);
}

// code->bytecode
bool CompileGeometryShader(const std::string& code, D3DBlob** blob, const D3D_SHADER_MACRO* pDefines)
{
	ID3D10Blob* shaderBuffer = nullptr;
	ID3D10Blob* errorBuffer = nullptr;

#if defined(_DEBUG) || defined(DEBUGFAST)
	UINT flags = D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY|D3D10_SHADER_DEBUG;
#else
	UINT flags = D3D10_SHADER_ENABLE_BACKWARDS_COMPATIBILITY|D3D10_SHADER_OPTIMIZATION_LEVEL3|D3D10_SHADER_SKIP_VALIDATION;
#endif
	HRESULT hr = PD3DCompile(code.c_str(), code.length(), nullptr, pDefines, nullptr, "main", D3D::GeometryShaderVersionString(),
							flags, 0, &shaderBuffer, &errorBuffer);

	if (errorBuffer)
	{
		INFO_LOG(VIDEO, "Geometry shader compiler messages:\n%s\n",
			(const char*)errorBuffer->GetBufferPointer());
	}

	if (FAILED(hr))
	{
		static int num_failures = 0;
		std::string filename = StringFromFormat("%sbad_gs_%04i.txt", File::GetUserPath(D_DUMP_IDX).c_str(), num_failures++);
		std::ofstream file;
		OpenFStream(file, filename, std::ios_base::out);
		file << code;
		file.close();

		PanicAlert("Failed to compile geometry shader: %s\nDebug info (%s):\n%s",
			filename.c_str(), D3D::GeometryShaderVersionString(), (const char*)errorBuffer->GetBufferPointer());

		*blob = nullptr;
		errorBuffer->Release();
	}
	else
	{
		*blob = new D3DBlob(shaderBuffer);
		shaderBuffer->Release();
	}
	return SUCCEEDED(hr);
}

// code->bytecode
bool CompilePixelShader(const std::string& code, D3DBlob** blob, const D3D_SHADER_MACRO* pDefines)
{
	ID3D10Blob* shaderBuffer = nullptr;
	ID3D10Blob* errorBuffer = nullptr;

#if defined(_DEBUG) || defined(DEBUGFAST)
	UINT flags = D3D10_SHADER_DEBUG;
#else
	UINT flags = D3D10_SHADER_OPTIMIZATION_LEVEL3;
#endif
	HRESULT hr = PD3DCompile(code.c_str(), code.length(), nullptr, pDefines, nullptr, "main", D3D::PixelShaderVersionString(),
							flags, 0, &shaderBuffer, &errorBuffer);

	if (errorBuffer)
	{
		INFO_LOG(VIDEO, "Pixel shader compiler messages:\n%s",
			(const char*)errorBuffer->GetBufferPointer());
	}

	if (FAILED(hr))
	{
		static int num_failures = 0;
		std::string filename = StringFromFormat("%sbad_ps_%04i.txt", File::GetUserPath(D_DUMP_IDX).c_str(), num_failures++);
		std::ofstream file;
		OpenFStream(file, filename, std::ios_base::out);
		file << code;
		file.close();

		PanicAlert("Failed to compile pixel shader: %s\nDebug info (%s):\n%s",
			filename.c_str(), D3D::PixelShaderVersionString(), (const char*)errorBuffer->GetBufferPointer());

		*blob = nullptr;
		errorBuffer->Release();
	}
	else
	{
		*blob = new D3DBlob(shaderBuffer);
		shaderBuffer->Release();
	}

	return SUCCEEDED(hr);
}

}  // namespace

}  // namespace DX12
