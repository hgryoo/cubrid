#pragma once

#include <array>
#include <cstddef>
#include <cmath>

namespace cubhnsw
{

  enum class vector_distance_metric_t
  {
    COSINE,
    EUCLIDEAN,
    MAX
  };

  using distance_t = float;
  using distance_fn_t = distance_t (*) (const float *, const float *, std::size_t);

  distance_t
  cubvec_cosine_distance (const float *vec1, const float *vec2, std::size_t dim);

  distance_t
  cubvec_l2_distance (const float *vec1, const float *vec2, std::size_t dim);

  extern const std::array<distance_fn_t,
	 static_cast<std::size_t> (vector_distance_metric_t::MAX)>
	 metric_table;


  // normalize
  inline bool
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

    #pragma omp simd
    for (std::size_t i = 0; i < dim; ++i)
      {
	vec[i] *= inv_norm;
      }

    return true;  // unit vector
  }

} // namespace cubhnsw
