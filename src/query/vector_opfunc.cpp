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

#include <stdexcept>
#include <cmath>
#include <cstddef>

#include "vector_opfunc.hpp"
#include "dbtype.h"
#include "dbtype_def.h"
#include "db_vector.hpp"
#include "vector_distance_enum.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/**
 * @brief Converts a DB_VALUE vector of floats into a std::vector<float>.
 *
 * This function extracts a set of float elements from the given DB_VALUE object,
 * which is expected to be of type DB_TYPE_VECTOR. It then constructs and returns
 * a std::vector<float> containing these elements.
 *
 * @param value A pointer to a DB_VALUE object that holds a vector.
 * @return std::vector<float> A vector containing the float elements extracted from the DB_VALUE.
 */
/* deprecated */
std::vector<float> db_value_get_stdvector_float (const DB_VALUE *value)
{
  /* The design of DB_TYPE_VECTOR has drastically changed. It is recommended that you use db_get_vector_float and access arr instead. */
  cubvec_log ("WARNING: This function is deprecated.");

  assert (value != nullptr && DB_VALUE_TYPE (value) == DB_TYPE_VECTOR);

  const DB_VECTOR_FLOAT *vf = db_get_vector_float (value);
  const auto dim = vf->dim;
  const auto arr = vf->float_array;

  return std::vector<float> (arr, arr + static_cast<size_t> (dim));
}


/**
 * @brief Computes the distance between two vector DB_VALUE objects using a specified metric.
 *
 * This function extracts two std::vector<float> from the provided DB_VALUE objects,
 * computes the distance between them based on the specified metric (default is cosine, though currently only
 * the Euclidean metric is supported), and stores the result in the provided DB_VALUE result.
 * If a third argument is provided, it is used to select the distance metric.
 *
 * @param result A pointer to a DB_VALUE where the computed distance will be stored.
 * @param args An array of pointers to DB_VALUE objects; expects exactly two vectors and a metric specifier.
 * @param num_args The number of arguments provided in the args array; should be 3.
 * @return int NO_ERROR if the computation is successful.
 */
int vector_distance (DB_VALUE *result, DB_VALUE *args[], int num_args)
{
  assert (num_args == 2 || num_args == 3);

  try
    {
      if (num_args == 2)
	{
	  // TODO: if index exists, use the metric used to create the index
	  // otherwise, use cosine distance as default
	  return vector_cosine_distance (result, args, 2);
	}

      assert (num_args == 3 && args[2] != nullptr && (DB_VALUE_TYPE (args[2]) == DB_TYPE_INTEGER));
      DB_VECTOR_DISTANCE_METRIC metric = static_cast<DB_VECTOR_DISTANCE_METRIC> (db_get_int (args[2]));

      // Use a switch statement to handle different metrics
      switch (metric)
	{
	case DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE:
	  return vector_cosine_distance (result, args, 2);

	case DB_VECTOR_DISTANCE_METRIC::METRIC_DOT:
	  return vector_negative_inner_product (result, args, 2);

	case DB_VECTOR_DISTANCE_METRIC::METRIC_EUCLIDEAN:
	  return vector_l2_distance (result, args, 2);

	case DB_VECTOR_DISTANCE_METRIC::METRIC_MANHATTAN:
	  return vector_l1_distance (result, args, 2);

	default:
	  throw std::invalid_argument ("Unsupported distance metric.");
	}
    }
  catch (const std::exception &e)
    {
      // TODO: handle this error with CUBRID error code.
      std::fprintf (stderr, "cubvec error: %s\n", e.what());
      std::abort();
    }
}

static int vector_distance_internal (DB_VALUE *result, DB_VALUE *args[], int num_args,
				     float (*distance_calculation) (const float *, const float *, size_t))
{
  // Ensure we have the correct number of arguments.
  assert (num_args == 2);

  if (DB_IS_NULL (args[0]) || DB_IS_NULL (args[1]))
    {
      db_make_null (result);
      return NO_ERROR;
    }

  const DB_VECTOR_FLOAT *vf1 = db_get_vector_float (args[0]);
  const auto dim1 = vf1->dim;
  const auto arr1 = vf1->float_array;

  const DB_VECTOR_FLOAT *vf2 = db_get_vector_float (args[1]);
  const auto arr2 = vf2->float_array;

  ASSERT_CUBVEC (vf1->dim == vf2->dim);

  float distance = 0.0f;
  try
    {
      distance = distance_calculation (arr1, arr2, dim1);
    }
  catch (const std::exception &e)
    {
      std::fprintf (stderr, "cubvec error: %s\n", e.what());
      std::abort();
    }

  if (std::isnan (distance))
    {
      db_make_null (result);
    }
  else
    {
      db_make_double (result, static_cast<double> (distance));
    }

  return NO_ERROR;
}

static inline float
cubvec_l1_distance (const float *__restrict vec1,
		    const float *__restrict vec2,
		    size_t dim)
{
  float sum = 0.0f;

  #pragma omp simd reduction(+:sum)
  for (size_t i = 0; i < dim; ++i)
    {
      float diff = vec1[i] - vec2[i];
      sum += std::fabs (diff);
    }

  return sum;
}

static inline float
cubvec_l2_sqr_distance (const float *__restrict vec1, const float *__restrict vec2, size_t dim)
{
  float sum = 0.0f;

  const float *__restrict v1 = vec1;
  const float *__restrict v2 = vec2;

  #pragma omp simd aligned(v1, v2:32) reduction(+:sum)
  for (size_t i = 0; i < dim; ++i)
    {
      float diff = v1[i] - v2[i];
      sum += diff * diff;
    }

  return sum;
}

static inline float
cubvec_l2_distance (const float *__restrict vec1, const float *__restrict vec2, size_t dim)
{
  return std::sqrt (cubvec_l2_sqr_distance (vec1, vec2, dim));
}

static inline float
cubvec_norm_L2sqr (const float *__restrict vec,
		   size_t dim)
{
  float sum = 0.0f;

  #pragma omp simd reduction(+:sum)
  for (size_t i = 0; i < dim; ++i)
    {
      float v = vec[i];
      sum += v * v;
    }

  return sum;
}

static inline float cubvec_inner_product (const float *vec1, const float *vec2, size_t dim)
{
  float sum = 0.0f;

  #pragma omp simd reduction(+:sum)
  for (std::size_t i = 0; i < dim; ++i)
    {
      sum += vec1[i] * vec2[i];
    }

  return sum;
}


static inline float
cubvec_cosine_distance (const float *__restrict vec1, const float *__restrict vec2, size_t dim)
{
  float dot = 0.0f;
  float norm1 = 0.0f;
  float norm2 = 0.0f;

  #pragma omp simd reduction(+:dot,norm1,norm2)
  for (size_t i = 0; i < dim; ++i)
    {
      float a = vec1[i];
      float b = vec2[i];

      dot   += a * b;
      norm1 += a * a;
      norm2 += b * b;
    }

  if (norm1 == 0.0f || norm2 == 0.0f)
    {
      return 1.0f;
    }

  float similarity = dot / sqrtf (norm1 * norm2);

  similarity = std::max (-1.0f, std::min (1.0f, similarity));

  return 1.0f - similarity;
}

int vector_l1_distance (DB_VALUE *result, DB_VALUE *args[], int num_args)
{
  return vector_distance_internal (result, args, num_args, cubvec_l1_distance);
}

int vector_l2_distance (DB_VALUE *result, DB_VALUE *args[], int num_args)
{
  return vector_distance_internal (result, args, num_args, cubvec_l2_distance);
}

int vector_inner_product (DB_VALUE *result, DB_VALUE *args[], int num_args)
{
  return vector_distance_internal (result, args, num_args, cubvec_inner_product);
}

int vector_negative_inner_product (DB_VALUE *result, DB_VALUE *args[], int num_args)
{
  int retval = vector_distance_internal (result, args, num_args, cubvec_inner_product);
  db_make_double (result, -db_get_double (result));
  return retval;
}

int vector_cosine_distance (DB_VALUE *result, DB_VALUE *args[], int num_args)
{
  return vector_distance_internal (result, args, num_args, cubvec_cosine_distance);
}
