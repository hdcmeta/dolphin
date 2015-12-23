// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DShader.h"
#include "VideoBackends/D3D12/StaticShaderCache.h"

#include "VideoCommon/VideoConfig.h"

namespace DX12
{

// Pixel Shader blobs
D3DBlob* s_ColorMatrixProgramBlob[2] = {};
D3DBlob* s_ColorCopyProgramBlob[2] = {};
D3DBlob* s_DepthMatrixProgramBlob[2] = {};
D3DBlob* s_DepthCopyProgramBlob[2] = {};
D3DBlob* s_ClearProgramBlob = {};
D3DBlob* s_AnaglyphProgramBlob = {};
D3DBlob* s_rgba6_to_rgb8Blob[2] = {};
D3DBlob* s_rgb8_to_rgba6Blob[2] = {};

// Vertex Shader blobs/input layouts
static D3DBlob* SimpleVertexShaderBlob = {};
static D3DBlob* ClearVertexShaderBlob = {};

static const D3D12_INPUT_ELEMENT_DESC SimpleVertexShaderInputElements[] = {
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

static const D3D12_INPUT_LAYOUT_DESC SimpleVertexShaderInputLayout = {
	SimpleVertexShaderInputElements,
	ARRAYSIZE(SimpleVertexShaderInputElements)
};

static const D3D12_INPUT_ELEMENT_DESC ClearVertexShaderInputElements[] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

static const D3D12_INPUT_LAYOUT_DESC ClearVertexShaderInputLayout =
{
	ClearVertexShaderInputElements,
	ARRAYSIZE(ClearVertexShaderInputElements)
};

// Geometry Shader blobs
D3DBlob* ClearGeometryShaderBlob = nullptr;
D3DBlob* CopyGeometryShaderBlob = nullptr;

// Pixel Shader HLSL
const char clear_program_code[] = {
	"void main(\n"
	"out float4 ocol0 : SV_Target,\n"
	"in float4 pos : SV_Position,\n"
	"in float4 incol0 : COLOR0){\n"
	"ocol0 = incol0;\n"
	"}\n"
};

// EXISTINGD3D11TODO: Find some way to avoid having separate shaders for non-MSAA and MSAA...
const char color_copy_program_code[] = {
	"sampler samp0 : register(s0);\n"
	"Texture2DArray Tex0 : register(t0);\n"
	"void main(\n"
	"out float4 ocol0 : SV_Target,\n"
	"in float4 pos : SV_Position,\n"
	"in float3 uv0 : TEXCOORD0){\n"
	"ocol0 = Tex0.Sample(samp0,uv0);\n"
	"}\n"
};

const char depth_copy_program_code[] = {
	"sampler samp0 : register(s0);\n"
	"Texture2DArray Tex0 : register(t0);\n"
	"void main(\n"
	"out float odepth : SV_Depth,\n"
	"in float4 pos : SV_Position,\n"
	"in float3 uv0 : TEXCOORD0){\n"
	"odepth = Tex0.Sample(samp0,uv0);\n"
	"}\n"
};

// Anaglyph Red-Cyan shader based on Dubois algorithm
// Constants taken from the paper:
// "Conversion of a Stereo Pair to Anaglyph with
// the Least-Squares Projection Method"
// Eric Dubois, March 2009
const char anaglyph_program_code[] = {
	"sampler samp0 : register(s0);\n"
	"Texture2DArray Tex0 : register(t0);\n"
	"void main(\n"
	"out float4 ocol0 : SV_Target,\n"
	"in float4 pos : SV_Position,\n"
	"in float3 uv0 : TEXCOORD0){\n"
	"float4 c0 = Tex0.Sample(samp0, float3(uv0.xy, 0.0));\n"
	"float4 c1 = Tex0.Sample(samp0, float3(uv0.xy, 1.0));\n"
	"float3x3 l = float3x3( 0.437, 0.449, 0.164,\n"
	"                      -0.062,-0.062,-0.024,\n"
	"                      -0.048,-0.050,-0.017);\n"
	"float3x3 r = float3x3(-0.011,-0.032,-0.007,\n"
	"                       0.377, 0.761, 0.009,\n"
	"                      -0.026,-0.093, 1.234);\n"
	"ocol0 = float4(mul(l, c0.rgb) + mul(r, c1.rgb), c0.a);\n"
	"}\n"
};

// TODO: Improve sampling algorithm!
const char color_copy_program_code_msaa[] = {
	"#define SAMPLES %d\n"
	"sampler samp0 : register(s0);\n"
	"Texture2DMSArray<float4, SAMPLES> Tex0 : register(t0);\n"
	"void main(\n"
	"out float4 ocol0 : SV_Target,\n"
	"in float4 pos : SV_Position,\n"
	"in float3 uv0 : TEXCOORD0){\n"
	"int width, height, slices, samples;\n"
	"Tex0.GetDimensions(width, height, slices, samples);\n"
	"ocol0 = 0;\n"
	"for(int i = 0; i < SAMPLES; ++i)\n"
	"	ocol0 += Tex0.Load(int3(uv0.x*(width), uv0.y*(height), uv0.z), i);\n"
	"ocol0 /= SAMPLES;\n"
	"}\n"
};

const char depth_copy_program_code_msaa[] = {
	"#define SAMPLES %d\n"
	"sampler samp0 : register(s0);\n"
	"Texture2DMSArray<float, SAMPLES> Tex0 : register(t0);\n"
	"void main(\n"
	"out float odepth : SV_Depth,\n"
	"in float4 pos : SV_Position,\n"
	"in float3 uv0 : TEXCOORD0){\n"
	"int width, height, slices, samples;\n"
	"Tex0.GetDimensions(width, height, slices, samples);\n"
	"odepth = 0;\n"
	"for(int i = 0; i < SAMPLES; ++i)\n"
	"	odepth += Tex0.Load(int3(uv0.x*(width), uv0.y*(height), uv0.z), i);\n"
	"odepth /= SAMPLES;\n"
	"}\n"
};

const char color_matrix_program_code[] = {
	"sampler samp0 : register(s0);\n"
	"Texture2DArray Tex0 : register(t0);\n"
	"uniform float4 cColMatrix[7] : register(c0);\n"
	"void main(\n"
	"out float4 ocol0 : SV_Target,\n"
	"in float4 pos : SV_Position,\n"
	"in float3 uv0 : TEXCOORD0){\n"
	"float4 texcol = Tex0.Sample(samp0,uv0);\n"
	"texcol = round(texcol * cColMatrix[5])*cColMatrix[6];\n"
	"ocol0 = float4(dot(texcol,cColMatrix[0]),dot(texcol,cColMatrix[1]),dot(texcol,cColMatrix[2]),dot(texcol,cColMatrix[3])) + cColMatrix[4];\n"
	"}\n"
};

const char color_matrix_program_code_msaa[] = {
	"#define SAMPLES %d\n"
	"sampler samp0 : register(s0);\n"
	"Texture2DMSArray<float4, SAMPLES> Tex0 : register(t0);\n"
	"uniform float4 cColMatrix[7] : register(c0);\n"
	"void main(\n"
	"out float4 ocol0 : SV_Target,\n"
	"in float4 pos : SV_Position,\n"
	"in float3 uv0 : TEXCOORD0){\n"
	"int width, height, slices, samples;\n"
	"Tex0.GetDimensions(width, height, slices, samples);\n"
	"float4 texcol = 0;\n"
	"for(int i = 0; i < SAMPLES; ++i)\n"
	"	texcol += Tex0.Load(int3(uv0.x*(width), uv0.y*(height), uv0.z), i);\n"
	"texcol /= SAMPLES;\n"
	"texcol = round(texcol * cColMatrix[5])*cColMatrix[6];\n"
	"ocol0 = float4(dot(texcol,cColMatrix[0]),dot(texcol,cColMatrix[1]),dot(texcol,cColMatrix[2]),dot(texcol,cColMatrix[3])) + cColMatrix[4];\n"
	"}\n"
};

const char depth_matrix_program[] = {
	"sampler samp0 : register(s0);\n"
	"Texture2DArray Tex0 : register(t0);\n"
	"uniform float4 cColMatrix[7] : register(c0);\n"
	"void main(\n"
	"out float4 ocol0 : SV_Target,\n"
	" in float4 pos : SV_Position,\n"
	" in float3 uv0 : TEXCOORD0){\n"
	"	float4 texcol = Tex0.Sample(samp0,uv0);\n"
	"	int depth = int((1.0 - texcol.x) * 16777216.0);\n"

	// Convert to Z24 format
	"	int4 workspace;\n"
	"	workspace.r = (depth >> 16) & 255;\n"
	"	workspace.g = (depth >> 8) & 255;\n"
	"	workspace.b = depth & 255;\n"

	// Convert to Z4 format
	"	workspace.a = (depth >> 16) & 0xF0;\n"

	// Normalize components to [0.0..1.0]
	"	texcol = float4(workspace) / 255.0;\n"

	// Apply color matrix
	"	ocol0 = float4(dot(texcol,cColMatrix[0]),dot(texcol,cColMatrix[1]),dot(texcol,cColMatrix[2]),dot(texcol,cColMatrix[3])) + cColMatrix[4];\n"
	"}\n"
};

const char depth_matrix_program_msaa[] = {
	"#define SAMPLES %d\n"
	"sampler samp0 : register(s0);\n"
	"Texture2DMSArray<float4, SAMPLES> Tex0 : register(t0);\n"
	"uniform float4 cColMatrix[7] : register(c0);\n"
	"void main(\n"
	"out float4 ocol0 : SV_Target,\n"
	" in float4 pos : SV_Position,\n"
	" in float3 uv0 : TEXCOORD0){\n"
	"	int width, height, slices, samples;\n"
	"	Tex0.GetDimensions(width, height, slices, samples);\n"
	"	float4 texcol = 0;\n"
	"	for(int i = 0; i < SAMPLES; ++i)\n"
	"		texcol += Tex0.Load(int3(uv0.x*(width), uv0.y*(height), uv0.z), i);\n"
	"	texcol /= SAMPLES;\n"
	"	int depth = int((1.0 - texcol.x) * 16777216.0);\n"

	// Convert to Z24 format
	"	int4 workspace;\n"
	"	workspace.r = (depth >> 16) & 255;\n"
	"	workspace.g = (depth >> 8) & 255;\n"
	"	workspace.b = depth & 255;\n"

	// Convert to Z4 format
	"	workspace.a = (depth >> 16) & 0xF0;\n"

	// Normalize components to [0.0..1.0]
	"	texcol = float4(workspace) / 255.0;\n"

	// Apply color matrix
	"	ocol0 = float4(dot(texcol,cColMatrix[0]),dot(texcol,cColMatrix[1]),dot(texcol,cColMatrix[2]),dot(texcol,cColMatrix[3])) + cColMatrix[4];\n"
	"}\n"
};

const char reint_rgba6_to_rgb8[] = {
	"sampler samp0 : register(s0);\n"
	"Texture2DArray Tex0 : register(t0);\n"
	"void main(\n"
	"	out float4 ocol0 : SV_Target,\n"
	"	in float4 pos : SV_Position,\n"
	"	in float3 uv0 : TEXCOORD0)\n"
	"{\n"
	"	int4 src6 = round(Tex0.Sample(samp0,uv0) * 63.f);\n"
	"	int4 dst8;\n"
	"	dst8.r = (src6.r << 2) | (src6.g >> 4);\n"
	"	dst8.g = ((src6.g & 0xF) << 4) | (src6.b >> 2);\n"
	"	dst8.b = ((src6.b & 0x3) << 6) | src6.a;\n"
	"	dst8.a = 255;\n"
	"	ocol0 = (float4)dst8 / 255.f;\n"
	"}"
};

const char reint_rgba6_to_rgb8_msaa[] = {
	"#define SAMPLES %d\n"
	"sampler samp0 : register(s0);\n"
	"Texture2DMSArray<float4, SAMPLES> Tex0 : register(t0);\n"
	"void main(\n"
	"	out float4 ocol0 : SV_Target,\n"
	"	in float4 pos : SV_Position,\n"
	"	in float3 uv0 : TEXCOORD0)\n"
	"{\n"
	"	int width, height, slices, samples;\n"
	"	Tex0.GetDimensions(width, height, slices, samples);\n"
	"	float4 texcol = 0;\n"
	"	for (int i = 0; i < SAMPLES; ++i)\n"
	"		texcol += Tex0.Load(int3(uv0.x*(width), uv0.y*(height), uv0.z), i);\n"
	"	texcol /= SAMPLES;\n"
	"	int4 src6 = round(texcol * 63.f);\n"
	"	int4 dst8;\n"
	"	dst8.r = (src6.r << 2) | (src6.g >> 4);\n"
	"	dst8.g = ((src6.g & 0xF) << 4) | (src6.b >> 2);\n"
	"	dst8.b = ((src6.b & 0x3) << 6) | src6.a;\n"
	"	dst8.a = 255;\n"
	"	ocol0 = (float4)dst8 / 255.f;\n"
	"}"
};

const char reint_rgb8_to_rgba6[] = {
	"sampler samp0 : register(s0);\n"
	"Texture2DArray Tex0 : register(t0);\n"
	"void main(\n"
	"	out float4 ocol0 : SV_Target,\n"
	"	in float4 pos : SV_Position,\n"
	"	in float3 uv0 : TEXCOORD0)\n"
	"{\n"
	"	int4 src8 = round(Tex0.Sample(samp0,uv0) * 255.f);\n"
	"	int4 dst6;\n"
	"	dst6.r = src8.r >> 2;\n"
	"	dst6.g = ((src8.r & 0x3) << 4) | (src8.g >> 4);\n"
	"	dst6.b = ((src8.g & 0xF) << 2) | (src8.b >> 6);\n"
	"	dst6.a = src8.b & 0x3F;\n"
	"	ocol0 = (float4)dst6 / 63.f;\n"
	"}\n"
};

const char reint_rgb8_to_rgba6_msaa[] = {
	"#define SAMPLES %d\n"
	"sampler samp0 : register(s0);\n"
	"Texture2DMSArray<float4, SAMPLES> Tex0 : register(t0);\n"
	"void main(\n"
	"	out float4 ocol0 : SV_Target,\n"
	"	in float4 pos : SV_Position,\n"
	"	in float3 uv0 : TEXCOORD0)\n"
	"{\n"
	"	int width, height, slices, samples;\n"
	"	Tex0.GetDimensions(width, height, slices, samples);\n"
	"	float4 texcol = 0;\n"
	"	for (int i = 0; i < SAMPLES; ++i)\n"
	"		texcol += Tex0.Load(int3(uv0.x*(width), uv0.y*(height), uv0.z), i);\n"
	"	texcol /= SAMPLES;\n"
	"	int4 src8 = round(texcol * 255.f);\n"
	"	int4 dst6;\n"
	"	dst6.r = src8.r >> 2;\n"
	"	dst6.g = ((src8.r & 0x3) << 4) | (src8.g >> 4);\n"
	"	dst6.b = ((src8.g & 0xF) << 2) | (src8.b >> 6);\n"
	"	dst6.a = src8.b & 0x3F;\n"
	"	ocol0 = (float4)dst6 / 63.f;\n"
	"}\n"
};

// Vertex Shader HLSL
const char simple_vertex_shader_hlsl[] = {
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

const char clear_vertex_shader_hlsl[] = {
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

// Geometry Shader HLSL
const char clear_geometry_shader_hlsl[] = {
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

const char copy_geometry_shader_hlsl[] = {
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

D3D12_SHADER_BYTECODE StaticShaderCache::GetReinterpRGBA6ToRGB8PixelShader(bool multisampled)
{
	D3D12_SHADER_BYTECODE bytecode = {};

	if (!multisampled || g_ActiveConfig.iMultisamples == 1)
	{
		if (!s_rgba6_to_rgb8Blob[0])
		{
			D3D::CompilePixelShader(reint_rgba6_to_rgb8, &s_rgba6_to_rgb8Blob[0]);
		}

		bytecode = { s_rgba6_to_rgb8Blob[0]->Data(), s_rgba6_to_rgb8Blob[0]->Size() };
		return bytecode;
	}
	else if (!s_rgba6_to_rgb8Blob[1])
	{
		// create MSAA shader for current AA mode
		std::string buf = StringFromFormat(reint_rgba6_to_rgb8_msaa, g_ActiveConfig.iMultisamples);

		D3D::CompilePixelShader(buf, &s_rgba6_to_rgb8Blob[1]);
		bytecode = { s_rgba6_to_rgb8Blob[1]->Data(), s_rgba6_to_rgb8Blob[1]->Size() };
	}
	return bytecode;
}

D3D12_SHADER_BYTECODE StaticShaderCache::GetReinterpRGB8ToRGBA6PixelShader(bool multisampled)
{
	D3D12_SHADER_BYTECODE bytecode = {};

	if (!multisampled || g_ActiveConfig.iMultisamples == 1)
	{
		if (!s_rgb8_to_rgba6Blob[0])
		{
			D3D::CompilePixelShader(reint_rgb8_to_rgba6, &s_rgb8_to_rgba6Blob[0]);
		}

		bytecode = { s_rgb8_to_rgba6Blob[0]->Data(), s_rgb8_to_rgba6Blob[0]->Size() };
		return bytecode;
	}
	else if (!s_rgb8_to_rgba6Blob[1])
	{
		// create MSAA shader for current AA mode
		std::string buf = StringFromFormat(reint_rgb8_to_rgba6_msaa, g_ActiveConfig.iMultisamples);

		D3D::CompilePixelShader(buf, &s_rgb8_to_rgba6Blob[1]);
		bytecode = { s_rgb8_to_rgba6Blob[1]->Data(), s_rgb8_to_rgba6Blob[1]->Size() };
	}

	return bytecode;
}

D3D12_SHADER_BYTECODE StaticShaderCache::GetColorCopyPixelShader(bool multisampled)
{
	D3D12_SHADER_BYTECODE bytecode = {};

	if (!multisampled || g_ActiveConfig.iMultisamples == 1)
	{
		bytecode = { s_ColorCopyProgramBlob[0]->Data(), s_ColorCopyProgramBlob[0]->Size() };
	}
	else if (s_ColorCopyProgramBlob[1])
	{
		bytecode = { s_ColorCopyProgramBlob[1]->Data(), s_ColorCopyProgramBlob[1]->Size() };
	}
	else
	{
		// create MSAA shader for current AA mode
		std::string buf = StringFromFormat(color_copy_program_code_msaa, g_ActiveConfig.iMultisamples);

		D3D::CompilePixelShader(buf, &s_ColorCopyProgramBlob[1]);
		bytecode = { s_ColorCopyProgramBlob[1]->Data(), s_ColorCopyProgramBlob[1]->Size() };
	}

	return bytecode;
}

D3D12_SHADER_BYTECODE StaticShaderCache::GetDepthCopyPixelShader(bool multisampled)
{
	D3D12_SHADER_BYTECODE bytecode = {};

	if (!multisampled || g_ActiveConfig.iMultisamples == 1)
	{
		bytecode = { s_DepthCopyProgramBlob[0]->Data(), s_DepthCopyProgramBlob[0]->Size() };
	}
	else if (s_DepthCopyProgramBlob[1])
	{
		bytecode = { s_DepthCopyProgramBlob[1]->Data(), s_DepthCopyProgramBlob[1]->Size() };
	}
	else
	{
		// create MSAA shader for current AA mode
		std::string buf = StringFromFormat(depth_copy_program_code_msaa, g_ActiveConfig.iMultisamples);

		D3D::CompilePixelShader(buf, &s_DepthCopyProgramBlob[1]);
		bytecode = { s_DepthCopyProgramBlob[1]->Data(), s_DepthCopyProgramBlob[1]->Size() };
	}

	return bytecode;
}

D3D12_SHADER_BYTECODE StaticShaderCache::GetColorMatrixPixelShader(bool multisampled)
{
	D3D12_SHADER_BYTECODE bytecode = {};

	if (!multisampled || g_ActiveConfig.iMultisamples == 1)
	{
		bytecode = { s_ColorMatrixProgramBlob[0]->Data(), s_ColorMatrixProgramBlob[0]->Size() };
	}
	else if (s_ColorMatrixProgramBlob[1])
	{
		bytecode = { s_ColorMatrixProgramBlob[1]->Data(), s_ColorMatrixProgramBlob[1]->Size() };
	}
	else
	{
		// create MSAA shader for current AA mode
		std::string buf = StringFromFormat(color_matrix_program_code_msaa, g_ActiveConfig.iMultisamples);

		D3D::CompilePixelShader(buf, &s_ColorMatrixProgramBlob[1]);
		bytecode = { s_ColorMatrixProgramBlob[1]->Data(), s_ColorMatrixProgramBlob[1]->Size() };
	}

	return bytecode;
}

D3D12_SHADER_BYTECODE StaticShaderCache::GetDepthMatrixPixelShader(bool multisampled)
{
	D3D12_SHADER_BYTECODE bytecode = {};

	if (!multisampled || g_ActiveConfig.iMultisamples == 1)
	{
		bytecode = { s_DepthMatrixProgramBlob[0]->Data(), s_DepthMatrixProgramBlob[0]->Size() };
	}
	else if (s_DepthMatrixProgramBlob[1])
	{
		bytecode = { s_DepthMatrixProgramBlob[1]->Data(), s_DepthMatrixProgramBlob[1]->Size() };
	}
	else
	{
		// create MSAA shader for current AA mode
		std::string buf = StringFromFormat(depth_matrix_program_msaa, g_ActiveConfig.iMultisamples);

		D3D::CompilePixelShader(buf, &s_DepthMatrixProgramBlob[1]);

		bytecode = { s_DepthMatrixProgramBlob[1]->Data(), s_DepthMatrixProgramBlob[1]->Size() };
	}

	return bytecode;
}

D3D12_SHADER_BYTECODE StaticShaderCache::GetClearPixelShader()
{
	D3D12_SHADER_BYTECODE shader = {};
	shader.BytecodeLength = s_ClearProgramBlob->Size();
	shader.pShaderBytecode = s_ClearProgramBlob->Data();

	return shader;
}

D3D12_SHADER_BYTECODE StaticShaderCache::GetAnaglyphPixelShader()
{
	D3D12_SHADER_BYTECODE shader = {};
	shader.BytecodeLength = s_AnaglyphProgramBlob->Size();
	shader.pShaderBytecode = s_AnaglyphProgramBlob->Data();

	return shader;
}

D3D12_SHADER_BYTECODE StaticShaderCache::GetSimpleVertexShader()
{
	D3D12_SHADER_BYTECODE shader = {};
	shader.BytecodeLength = SimpleVertexShaderBlob->Size();
	shader.pShaderBytecode = SimpleVertexShaderBlob->Data();

	return shader;
}

D3D12_SHADER_BYTECODE StaticShaderCache::GetClearVertexShader()
{
	D3D12_SHADER_BYTECODE shader = {};
	shader.BytecodeLength = ClearVertexShaderBlob->Size();
	shader.pShaderBytecode = ClearVertexShaderBlob->Data();

	return shader;
}

D3D12_INPUT_LAYOUT_DESC StaticShaderCache::GetSimpleVertexShaderInputLayout()
{
	return SimpleVertexShaderInputLayout;
}

D3D12_INPUT_LAYOUT_DESC StaticShaderCache::GetClearVertexShaderInputLayout()
{
	return ClearVertexShaderInputLayout;
}

D3D12_SHADER_BYTECODE StaticShaderCache::GetClearGeometryShader()
{
	D3D12_SHADER_BYTECODE bytecode = {};
	if (g_ActiveConfig.iStereoMode > 0)
	{
		bytecode.BytecodeLength = ClearGeometryShaderBlob->Size();
		bytecode.pShaderBytecode = ClearGeometryShaderBlob->Data();
	}

	return bytecode;
}

D3D12_SHADER_BYTECODE StaticShaderCache::GetCopyGeometryShader()
{
	D3D12_SHADER_BYTECODE bytecode = {};
	if (g_ActiveConfig.iStereoMode > 0)
	{
		bytecode.BytecodeLength = CopyGeometryShaderBlob->Size();
		bytecode.pShaderBytecode = CopyGeometryShaderBlob->Data();
	}

	return bytecode;
}

void StaticShaderCache::Init()
{
	// Compile static pixel shaders
	D3D::CompilePixelShader(clear_program_code, &s_ClearProgramBlob);
	D3D::CompilePixelShader(anaglyph_program_code, &s_AnaglyphProgramBlob);
	D3D::CompilePixelShader(color_copy_program_code, &s_ColorCopyProgramBlob[0]);
	D3D::CompilePixelShader(depth_copy_program_code, &s_DepthCopyProgramBlob[0]);
	D3D::CompilePixelShader(color_matrix_program_code, &s_ColorMatrixProgramBlob[0]);
	D3D::CompilePixelShader(depth_matrix_program, &s_DepthMatrixProgramBlob[0]);
	
	// Compile static vertex shaders
	D3D::CompileVertexShader(simple_vertex_shader_hlsl, &SimpleVertexShaderBlob);
	D3D::CompileVertexShader(clear_vertex_shader_hlsl, &ClearVertexShaderBlob);

	// Compile static geometry shaders
	D3D::CompileGeometryShader(clear_geometry_shader_hlsl, &ClearGeometryShaderBlob);
	D3D::CompileGeometryShader(copy_geometry_shader_hlsl, &CopyGeometryShaderBlob);
}

// Call this when multisampling mode changes, and shaders need to be regenerated.
void StaticShaderCache::InvalidateMSAAShaders()
{
	SAFE_RELEASE(s_ColorCopyProgramBlob[1]);
	SAFE_RELEASE(s_ColorMatrixProgramBlob[1]);
	SAFE_RELEASE(s_DepthMatrixProgramBlob[1]);
	SAFE_RELEASE(s_rgb8_to_rgba6Blob[1]);
	SAFE_RELEASE(s_rgba6_to_rgb8Blob[1]);
}

void StaticShaderCache::Shutdown()
{
	// Free pixel shader blobs

	SAFE_RELEASE(s_ClearProgramBlob);
	SAFE_RELEASE(s_AnaglyphProgramBlob);

	for (unsigned int i = 0; i < 2; ++i)
	{
		SAFE_RELEASE(s_ColorCopyProgramBlob[i]);
		SAFE_RELEASE(s_ColorMatrixProgramBlob[i]);
		SAFE_RELEASE(s_DepthMatrixProgramBlob[i]);
		SAFE_RELEASE(s_rgba6_to_rgb8Blob[i]);
		SAFE_RELEASE(s_rgb8_to_rgba6Blob[i]);
	}

	// Free vertex shader blobs

	SAFE_RELEASE(SimpleVertexShaderBlob);
	SAFE_RELEASE(ClearVertexShaderBlob);

	// Free geometry shader blobs

	ClearGeometryShaderBlob->Release();
	CopyGeometryShaderBlob->Release();
}

}