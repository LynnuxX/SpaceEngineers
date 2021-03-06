#include <common.h>

SamplerComparisonState	ShadowmapSampler	: register( MERGE(s,SHADOW_SAMPLER_SLOT) );

const static int CASCADES_NUM = 4;
const static int MAX_CASCADES = 7;

struct CsmConstants {
	matrix 	cascade_matrix[8];
	float4 	split_dist[2];
	float4 	cascade_scale[4]; 
};
cbuffer CSM : register ( b4 )
{
	CsmConstants csm_;	
}

#ifndef CUSTOM_CASCADE_SLOT
Texture2DArray<float> CSM : register( MERGE(t,CASCADES_SM_SLOT) );
#endif

static const float cascade_size = 1024.f;
static const float zbias = 0;//0.0015f;

static const float F_INF = pow(2, 50);

float cascade_index_by_split(float linear_depth)
{
	float4 near = float4(csm_.split_dist[0]);
	float4 far = float4(csm_.split_dist[0].yzw, csm_.split_dist[1].x);

	float index = dot( (linear_depth >= near) * (linear_depth < far), float4(0,1,2,3));

	near = float4(csm_.split_dist[1]);
	far = float4(csm_.split_dist[1].yzw, F_INF);

	index = min(index, dot( (linear_depth >= near) * (linear_depth < far), float4(4,5,6,7)) );

	return index;
}

uint cascade_id_stencil(uint stencil)
{
	return 31 - firstbithigh(reversebits(stencil));	
}

float3 world_to_shadowmap(float3 wpos, matrix mat)
{
	float4 pos = mul(float4(wpos, 1), mat);
	return pos.xyz / pos.w;
}

static const float2 Poisson_samples[] = {
	float2( 0.130697, -0.209628),
	float2( -0.112312, 0.327448),
	float2( -0.499089, -0.030236),
	float2( 0.332994, 0.380106),
	float2( -0.234209, -0.557516),
	float2( 0.695785, 0.066096),
	float2( -0.419485, 0.632050),
	float2( 0.678688, -0.447710),
	float2( 0.333877, 0.807633),
	float2( -0.834613, 0.383171),
	float2( -0.682884, -0.637443),
	float2( 0.769794, 0.568801),
	float2( -0.087941, -0.955035),
	float2( -0.947188, -0.166568),
	float2( 0.425303, -0.874130),
	float2( -0.134360, 0.982611),
};

float calculate_shadow(float3 world_pos, uint stencil)
{
	uint c_id = cascade_id_stencil(stencil);
	float3 lpos = world_to_shadowmap(world_pos, csm_.cascade_matrix[c_id]);

	float3 lpos_dx = ddx_fine(lpos);
    float3 lpos_dy = ddy_fine(lpos);

    float texelsize = 1/cascade_size;

    float Max_kernel = 4;
    float Filter_size = 3;


    float4 cascade_scale_line = csm_.cascade_scale[c_id / 2];
    float2 ratio = abs(((c_id & 1)==0 ? cascade_scale_line.xy : cascade_scale_line.zw) / csm_.cascade_scale[0].xy);
    ratio = exp2(ratio) / 2;
    float2 filter_size = clamp(ratio * Filter_size, 1, Max_kernel);

    lpos.z -= zbias;

    float result = 0;
    [branch]
    if(any(filter_size > 1)) {
    	float2 scale = filter_size * 0.5f * texelsize;
    	uint samples_num = 16;

    	[unroll]
    	for(uint s=0; s<samples_num; s++) {
    		float2 sample_pos = lpos.xy + texelsize * 2 * Poisson_samples[s];

    		result += CSM.SampleCmpLevelZero(ShadowmapSampler, float3(sample_pos, c_id), lpos.z);
    	}

    	return pow(result / (float)samples_num, 4);
    }
    else {
    	return CSM.SampleCmpLevelZero(ShadowmapSampler, float3(lpos.xy, c_id), lpos.z);
    }
}

float calculate_shadow_fast(float3 world_pos, uint stencil)
{
	uint c_id = cascade_id_stencil(stencil);
	float3 lpos = world_to_shadowmap(world_pos, csm_.cascade_matrix[c_id]);
	lpos.z -= zbias;
	return CSM.SampleCmpLevelZero(ShadowmapSampler, float3(lpos.xy, c_id), lpos.z) + any(lpos.xy != saturate(lpos.xy));
}

float calculate_shadow_fast_particle(float3 world_pos, float depth)
{
	uint c_id = cascade_index_by_split(depth);
	float3 lpos = world_to_shadowmap(world_pos, csm_.cascade_matrix[c_id]);
	lpos.z -= zbias;
	return CSM.SampleCmpLevelZero(ShadowmapSampler, float3(lpos.xy, c_id), lpos.z);
}

float calculate_shadow_fast_aprox(float3 world_pos)
{
	float3 lpos = world_to_shadowmap(world_pos, csm_.cascade_matrix[0]);
	lpos.z -= zbias;
	return CSM.SampleCmpLevelZero(ShadowmapSampler, float3(lpos.xy, 0), lpos.z);	
}