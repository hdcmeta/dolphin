// Copyright 2012 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/PerfQuery.h"
#include "VideoCommon/RenderBase.h"

namespace DX12 {

PerfQuery::PerfQuery()
	: m_query_read_pos()
{
	for (ActiveQuery& entry : m_query_buffer)
	{
#ifdef USE_D3D11
		D3D11_QUERY_DESC qdesc = CD3D11_QUERY_DESC(D3D11_QUERY_OCCLUSION, 0);
		D3D::device->CreateQuery(&qdesc, &entry.query);
#endif
	}

	ResetQuery();
}

PerfQuery::~PerfQuery()
{
	for (ActiveQuery& entry : m_query_buffer)
	{
		// TODO: EndQuery?
#ifdef USE_D3D11
		entry.query->Release();
#endif
	}
}

void PerfQuery::EnableQuery(PerfQueryGroup type)
{
	// Is this sane?
	if (m_query_count > m_query_buffer.size() / 2)
		WeakFlush();

	if (m_query_buffer.size() == m_query_count)
	{
		// TODO
		FlushOne();
		ERROR_LOG(VIDEO, "Flushed query buffer early!");
	}

	// start query
	if (type == PQG_ZCOMP_ZCOMPLOC || type == PQG_ZCOMP)
	{
		auto& entry = m_query_buffer[(m_query_read_pos + m_query_count) % m_query_buffer.size()];
#ifdef USE_D3D11
		D3D::context->Begin(entry.query);
#endif
		entry.query_type = type;

		++m_query_count;
	}
}

void PerfQuery::DisableQuery(PerfQueryGroup type)
{
	// stop query
	if (type == PQG_ZCOMP_ZCOMPLOC || type == PQG_ZCOMP)
	{
		auto& entry = m_query_buffer[(m_query_read_pos + m_query_count + m_query_buffer.size()-1) % m_query_buffer.size()];
#ifdef USE_D3D11
		D3D::context->End(entry.query);
#endif
	}
}

void PerfQuery::ResetQuery()
{
	m_query_count = 0;
	std::fill_n(m_results, ArraySize(m_results), 0);
}

u32 PerfQuery::GetQueryResult(PerfQueryType type)
{
	u32 result = 0;

	if (type == PQ_ZCOMP_INPUT_ZCOMPLOC || type == PQ_ZCOMP_OUTPUT_ZCOMPLOC)
		result = m_results[PQG_ZCOMP_ZCOMPLOC];
	else if (type == PQ_ZCOMP_INPUT || type == PQ_ZCOMP_OUTPUT)
		result = m_results[PQG_ZCOMP];
	else if (type == PQ_BLEND_INPUT)
		result = m_results[PQG_ZCOMP] + m_results[PQG_ZCOMP_ZCOMPLOC];
	else if (type == PQ_EFB_COPY_CLOCKS)
		result = m_results[PQG_EFB_COPY_CLOCKS];

	return result / 4;
}

void PerfQuery::FlushOne()
{
	auto& entry = m_query_buffer[m_query_read_pos];

	UINT64 result = 0;
#ifdef USE_D3D11
	HRESULT hr = S_FALSE;
#else
	HRESULT hr = S_OK;
#endif
	while (hr != S_OK)
	{
		// TODO: Might cause us to be stuck in an infinite loop!
#ifdef USE_D3D11
		hr = D3D::context->GetData(entry.query, &result, sizeof(result), 0);
#endif
	}

	// NOTE: Reported pixel metrics should be referenced to native resolution
	m_results[entry.query_type] += (u32)(result * EFB_WIDTH / g_renderer->GetTargetWidth() * EFB_HEIGHT / g_renderer->GetTargetHeight());

	m_query_read_pos = (m_query_read_pos + 1) % m_query_buffer.size();
	--m_query_count;
}

// TODO: could selectively flush things, but I don't think that will do much
void PerfQuery::FlushResults()
{
	while (!IsFlushed())
		FlushOne();
}

void PerfQuery::WeakFlush()
{
	while (!IsFlushed())
	{
		auto& entry = m_query_buffer[m_query_read_pos];

		UINT64 result = 0;
		HRESULT hr = S_OK;
#ifdef USE_D3D11
		hr = D3D::context->GetData(entry.query, &result, sizeof(result), D3D11_ASYNC_GETDATA_DONOTFLUSH);
#endif

		if (hr == S_OK)
		{
			// NOTE: Reported pixel metrics should be referenced to native resolution
			m_results[entry.query_type] += (u32)(result * EFB_WIDTH / g_renderer->GetTargetWidth() * EFB_HEIGHT / g_renderer->GetTargetHeight());

			m_query_read_pos = (m_query_read_pos + 1) % m_query_buffer.size();
			--m_query_count;
		}
		else
		{
			break;
		}
	}
}

bool PerfQuery::IsFlushed() const
{
	return 0 == m_query_count;
}

} // namespace