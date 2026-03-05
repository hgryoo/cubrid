/*
 *
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include "vector_distance.hpp"

#include <immintrin.h>
#include <cstddef>
#include <cstdint>

#include <cmath>
#include <omp.h>
#include <algorithm>

#include "porting_inline.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static inline bool is_aligned_32 (const void *p)
{
  return (reinterpret_cast<std::uintptr_t> (p) & 31u) == 0u;
}

static inline float hsum256_ps (__m256 v)
{
  // Horizontal sum of 8 floats
  __m128 lo = _mm256_castps256_ps128 (v);
  __m128 hi = _mm256_extractf128_ps (v, 1);
  __m128 sum = _mm_add_ps (lo, hi);                // 4 lanes
  __m128 shuf = _mm_movehdup_ps (sum);             // (sum3,sum3,sum1,sum1)
  sum = _mm_add_ps (sum, shuf);
  shuf = _mm_movehl_ps (shuf, sum);
  sum = _mm_add_ss (sum, shuf);
  return _mm_cvtss_f32 (sum);
}

static inline float dot_avx2_dim256 (const float *__restrict a, const float *__restrict b)
{
  // 256 floats = 32 * 8-float vectors.
  // Use 4 independent accumulators (latency hiding).
  __m256 acc0 = _mm256_setzero_ps ();
  __m256 acc1 = _mm256_setzero_ps ();
  __m256 acc2 = _mm256_setzero_ps ();
  __m256 acc3 = _mm256_setzero_ps ();

  const bool aligned = is_aligned_32 (a) && is_aligned_32 (b);

  // Unroll by 4 vectors => 32/4 = 8 iterations
  for (std::size_t i = 0; i < 256; i += 32)
    {
      __m256 va0, vb0, va1, vb1, va2, vb2, va3, vb3;

      if (aligned)
	{
	  va0 = _mm256_load_ps (a + i + 0);
	  vb0 = _mm256_load_ps (b + i + 0);
	  va1 = _mm256_load_ps (a + i + 8);
	  vb1 = _mm256_load_ps (b + i + 8);
	  va2 = _mm256_load_ps (a + i + 16);
	  vb2 = _mm256_load_ps (b + i + 16);
	  va3 = _mm256_load_ps (a + i + 24);
	  vb3 = _mm256_load_ps (b + i + 24);
	}
      else
	{
	  va0 = _mm256_loadu_ps (a + i + 0);
	  vb0 = _mm256_loadu_ps (b + i + 0);
	  va1 = _mm256_loadu_ps (a + i + 8);
	  vb1 = _mm256_loadu_ps (b + i + 8);
	  va2 = _mm256_loadu_ps (a + i + 16);
	  vb2 = _mm256_loadu_ps (b + i + 16);
	  va3 = _mm256_loadu_ps (a + i + 24);
	  vb3 = _mm256_loadu_ps (b + i + 24);
	}

      // FMA: acc += a*b
      acc0 = _mm256_fmadd_ps (va0, vb0, acc0);
      acc1 = _mm256_fmadd_ps (va1, vb1, acc1);
      acc2 = _mm256_fmadd_ps (va2, vb2, acc2);
      acc3 = _mm256_fmadd_ps (va3, vb3, acc3);
    }

  __m256 acc = _mm256_add_ps (_mm256_add_ps (acc0, acc1), _mm256_add_ps (acc2, acc3));
  return hsum256_ps (acc);
}

static inline float norm2_avx2_dim256 (const float *__restrict v)
{
  // Computes sum(v[i]*v[i]) for 256 floats.
  __m256 acc0 = _mm256_setzero_ps ();
  __m256 acc1 = _mm256_setzero_ps ();
  __m256 acc2 = _mm256_setzero_ps ();
  __m256 acc3 = _mm256_setzero_ps ();

  const bool aligned = is_aligned_32 (v);

  for (std::size_t i = 0; i < 256; i += 32)
    {
      __m256 x0, x1, x2, x3;
      if (aligned)
	{
	  x0 = _mm256_load_ps (v + i + 0);
	  x1 = _mm256_load_ps (v + i + 8);
	  x2 = _mm256_load_ps (v + i + 16);
	  x3 = _mm256_load_ps (v + i + 24);
	}
      else
	{
	  x0 = _mm256_loadu_ps (v + i + 0);
	  x1 = _mm256_loadu_ps (v + i + 8);
	  x2 = _mm256_loadu_ps (v + i + 16);
	  x3 = _mm256_loadu_ps (v + i + 24);
	}

      acc0 = _mm256_fmadd_ps (x0, x0, acc0);
      acc1 = _mm256_fmadd_ps (x1, x1, acc1);
      acc2 = _mm256_fmadd_ps (x2, x2, acc2);
      acc3 = _mm256_fmadd_ps (x3, x3, acc3);
    }

  __m256 acc = _mm256_add_ps (_mm256_add_ps (acc0, acc1), _mm256_add_ps (acc2, acc3));
  return hsum256_ps (acc);
}

static inline void scale_avx2_dim256 (float *__restrict v, float s)
{
  const bool aligned = is_aligned_32 (v);
  __m256 vs = _mm256_set1_ps (s);

  for (std::size_t i = 0; i < 256; i += 32)
    {
      __m256 x0, x1, x2, x3;

      if (aligned)
	{
	  x0 = _mm256_load_ps (v + i + 0);
	  x1 = _mm256_load_ps (v + i + 8);
	  x2 = _mm256_load_ps (v + i + 16);
	  x3 = _mm256_load_ps (v + i + 24);

	  x0 = _mm256_mul_ps (x0, vs);
	  x1 = _mm256_mul_ps (x1, vs);
	  x2 = _mm256_mul_ps (x2, vs);
	  x3 = _mm256_mul_ps (x3, vs);

	  _mm256_store_ps (v + i + 0, x0);
	  _mm256_store_ps (v + i + 8, x1);
	  _mm256_store_ps (v + i + 16, x2);
	  _mm256_store_ps (v + i + 24, x3);
	}
      else
	{
	  x0 = _mm256_loadu_ps (v + i + 0);
	  x1 = _mm256_loadu_ps (v + i + 8);
	  x2 = _mm256_loadu_ps (v + i + 16);
	  x3 = _mm256_loadu_ps (v + i + 24);

	  x0 = _mm256_mul_ps (x0, vs);
	  x1 = _mm256_mul_ps (x1, vs);
	  x2 = _mm256_mul_ps (x2, vs);
	  x3 = _mm256_mul_ps (x3, vs);

	  _mm256_storeu_ps (v + i + 0, x0);
	  _mm256_storeu_ps (v + i + 8, x1);
	  _mm256_storeu_ps (v + i + 16, x2);
	  _mm256_storeu_ps (v + i + 24, x3);
	}
    }
}

namespace cubhnsw
{
  bool
  cubvec_cosine_normalize (float *__restrict vec, std::size_t dim)
  {
    float norm_sq = 0.0f;

    #pragma omp simd reduction(+ : norm_sq)
    for (std::size_t i = 0; i < dim; ++i)
      {
	norm_sq += vec[i] * vec[i];
      }

    constexpr float eps = 1e-12f;
    if (norm_sq < eps)
      {
	// zero / near-zero vector is invalid for cosine/IP
	return false;
      }

    const float inv_norm = 1.0f / std::sqrt (norm_sq);

    if (dim == 256)
      {
	scale_avx2_dim256 (vec, inv_norm);
      }
    else
      {
	#pragma omp simd
	for (std::size_t i = 0; i < dim; ++i)
	  {
	    vec[i] *= inv_norm;
	  }
      }

    return true;  // unit vector
  }

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_cosine_distance (const float *vec1, const float *vec2, std::size_t dim)
  {
    float acc0=0, acc1=0, acc2=0, acc3=0;
    float dot = 0;

    if (dim == 256)
      {
	dot = dot_avx2_dim256 (vec1, vec2);
      }
    else
      {
	#pragma omp simd reduction(+ : acc0, acc1, acc2, acc3)
	for (size_t i = 0; i < dim; i += 4)
	  {
	    acc0 += vec1[i]   * vec2[i];
	    acc1 += vec1[i+1] * vec2[i+1];
	    acc2 += vec1[i+2] * vec2[i+2];
	    acc3 += vec1[i+3] * vec2[i+3];
	  }
	dot = acc0 + acc1 + acc2 + acc3;
      }

    return 1.0f - dot;
  }

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_inner_product_distance (const float *vec1, const float *vec2, std::size_t dim)
  {
    float sum = 0.0f;

    #pragma omp simd reduction(+ : sum)
    for (std::size_t i = 0; i < dim; ++i)
      {
	sum += vec1[i] * vec2[i];
      }
    return sum;
  };

  STATIC_INLINE distance_t __attribute__ ((ALWAYS_INLINE))
  cubvec_l2_distance (const float *vec1, const float *vec2, std::size_t dim)
  {
    float sum = 0.0f;
    #pragma omp simd reduction(+ : sum)
    for (std::size_t i = 0; i < dim; ++i)
      {
	const float d = vec1[i] - vec2[i];
	sum += d * d;
      }
    return sum;
  }

  const std::array<distance_fn_t,
	static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	metric_table =
  {
    cubvec_cosine_distance,
    cubvec_l2_distance,
    cubvec_inner_product_distance
  };

} // namespace cubhnsw
