#include "db_spatial.hpp"

#include "db_geos.hpp"
#include "dbtype.h"
#include "memory_alloc.h"
#include "memory_private_allocator.hpp"
#include "object_primitive.h"
#include "object_representation.h"
#include "porting_inline.hpp"
#include "query_dump.h"
#include "string_opfunc.h"
#include "system_parameter.h"

#include <algorithm>
#include <string>
#include <sstream>
#include <stack>

int 
db_spatial_serialize (const CUB_GEOMETRY &geom, or_buf &buffer)
{
    GEOS_WKBWRITER writer;

    std::ostringstream oss;
    writer.write(geom, oss);

    std::string serialized = oss.str ();
    
    or_put_int (&buffer, serialized.size());
    or_put_data (&buffer, serialized.data(), serialized.size());

    return NO_ERROR;
}

std::size_t 
db_spatial_serialize_length (const CUB_GEOMETRY &geom)
{
    GEOS_WKBWRITER writer;

    std::ostringstream oss;
    writer.write(geom, oss);

    oss.seekp(0, std::ios::end);
    std::size_t size = oss.tellp();

    return size;
}

int 
db_spatial_deserialize (or_buf *buf, CUB_GEOMETRY *&geom)
{
  int error_code = NO_ERROR;

  GEOS_WKBREADER reader;
  std::istringstream iss;

  int size = OR_GET_INT (buf);
  char *char_buf = new char[size];
  or_get_data (buf, char_buf, size);

  std::string str_buf (char_buf);
  iss.str (str_buf);
  delete char_buf;

  auto geom_ptr = reader.read (iss);
  geom = dynamic_cast<CUB_GEOMETRY*>(geom_ptr.release ());

  return error_code;
}

DB_GEOMETRY_TYPE
db_geometry_get_type (CUB_GEOMETRY *&geom)
{

  if (geom == NULL)
  {
    return DB_GEOMETRY_NULL;
  }

  CUB_GEOMETRY_TYPE_ID id = geom->getGeometryTypeId ();

  using namespace geos::geom;
  switch (id)
  {
    case GEOS_POINT:
      return DB_GEOMETRY_POINT;

    case GEOS_LINESTRING:
    case GEOS_LINEARRING:
      return DB_GEOMETRY_LINESTRING;

    case GEOS_POLYGON:
      return DB_GEOMETRY_POLYGON;

    case GEOS_MULTIPOINT:
      return DB_GEOMETRY_MULTIPOINT;

    case GEOS_MULTILINESTRING:
      return DB_GEOMETRY_MULTILINESTRING;

    case GEOS_MULTIPOLYGON:
      return DB_GEOMETRY_MULTIPOLYGON;

    case CUB_GEOMETRYCOLLECTION:
      return DB_GEOMETRY_GEOMETRYCOLLECTION;

    default:
      return DB_GEOMETRY_UNKNOWN;
  }
}