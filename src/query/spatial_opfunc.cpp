#include "spatial_opfunc.hpp"

#include "config.h"

#include "db_spatial.hpp"

#include "dbtype_def.h"
#include "dbtype.h"
#include "object_primitive.h"
#include "object_representation.h"

int
db_spatial_geometry_from_text (DB_VALUE * result, DB_VALUE * args[], int const num_args)
{
  int error_status = NO_ERROR;
  {
    for (int i = 0; i < num_args; i++)
      {
	DB_VALUE *arg = args[i];
	/* check for allocated DB value */
	assert (arg != (DB_VALUE *) NULL);

	/* if any argument is NULL, return NULL */
	if (DB_IS_NULL (arg))
	  {
	    db_make_null (result);
	    goto exit;
	  }
      }

    const DB_VALUE *text = args[0];

    /* check type */
    if (!is_char_string (text))
      {
	error_status = ER_QSTR_INVALID_DATA_TYPE;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_status, 0);
	goto exit;
      }

    std::string wkt_string (db_get_string (text), db_get_string_size (text));
    GEOS_WKTREADER reader;

    auto geom_ptr = reader.read (wkt_string)
    CUB_GEOMETRY *geom = geom.release ()

    db_make_geometry (result, geom, false);
  }

exit:
  if (error_status != NO_ERROR)
    {

    }

  return error_status;
}

int db_spatial_geometry_as_text (DB_VALUE * result, DB_VALUE * args[], const int num_args)
{
  int error_status = NO_ERROR;
  {
    for (int i = 0; i < num_args; i++)
      {
	DB_VALUE *arg = args[i];
	/* check for allocated DB value */
	assert (arg != (DB_VALUE *) NULL);

	/* if any argument is NULL, return NULL */
	if (DB_IS_NULL (arg))
	  {
	    db_make_null (result);
	    goto exit;
	  }
      }

    const DB_VALUE *geom = args[0];

    /* check is geometry */
    // TODO

    CUB_GEOMETRY *geom_instance = db_get_geometry (geom);

    std::string wkt_string (db_get_string (text), db_get_string_size (text));
    GEOS_WKTREADER reader;

    auto geom_ptr = reader.read (wkt_string)
    CUB_GEOMETRY *geom = geom.release ()

    db_make_geometry (result, geom, false);
  }

exit:
  if (error_status != NO_ERROR)
    {

    }

  return error_status;
}
