/*
 * Copyright 2008 Search Solution Corporation
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

/*
 * parse_type.cpp
 */

#include "parse_type.hpp"
#include "parser.h"
#include "string_buffer.hpp"

const char *pt_generic_type_to_string (pt_generic_type_enum type)
{
  static const char *arr[] =
  {
    "GENERIC TYPE NONE",
    "GENERIC ANY STRING TYPE",
    "GENERIC VARIABLE STRING TYPE",
    "GENERIC ANY CHAR TYPE",
    "GENERIC ANY NCHAR TYPE",
    "GENERIC ANY BIT TYPE",
    "GENERIC DISCRETE NUMBER TYPE",
    "GENERIC ANY NUMBER TYPE",
    "GENERIC DATE TYPE",
    "GENERIC DATETIME TYPE",
    "GENERIC SEQUENCE TYPE",
    "GENERIC LOB TYPE",
    "GENERIC QUERY TYPE",    // what is this?
    "GENERIC PRIMITIVE TYPE",
    "GENERIC ANY TYPE",
    "JSON VALUE",
    "JSON DOCUMENT",
    "GENERIC SCALAR TYPE",
  };
  static_assert ((PT_GENERIC_TYPE_SCALAR + 1) == (sizeof (arr) / sizeof (const char *)),
		 "miss-match between pt_generic_type_enum and its name array");
  return arr[type];
}

//--------------------------------------------------------------------------------
const char *pt_arg_type_to_string_buffer (const pt_arg_type &type, string_buffer &sb)
{
  switch (type.type)
    {
    case pt_arg_type::NORMAL:
      //sb("%s", str(type.val.type));
      sb ("%s", pt_show_type_enum (type.val.type));
      break;
    case pt_arg_type::GENERIC:
      sb ("%s", pt_generic_type_to_string (type.val.generic_type));
      break;
    case pt_arg_type::INDEX:
      sb ("IDX%d", type.val.index);
      break;
    }
  return sb.get_buffer();
}


/*
 * pt_are_equivalent_types () - check if a node type is equivalent with a
 *				definition type
 * return	: true if the types are equivalent, false otherwise
 * def_type(in)	: the definition type
 * op_type(in)	: argument type
 */
bool
pt_are_equivalent_types (const PT_ARG_TYPE def_type, const PT_TYPE_ENUM op_type)
{
  if (def_type.type == pt_arg_type::NORMAL)
    {
      if (def_type.val.type == op_type && op_type == PT_TYPE_NONE)
	{
	  /* return false if both arguments are of type none */
	  return false;
	}
      if (def_type.val.type == op_type)
	{
	  /* return true if both have the same type */
	  return true;
	}
      /* if def_type is a PT_TYPE_ENUM and the conditions above did not hold then the two types are not equivalent. */
      return false;
    }

  switch (def_type.val.generic_type)
    {
    case PT_GENERIC_TYPE_ANY:
      /* PT_GENERIC_TYPE_ANY is equivalent to any type */
      return true;
    case PT_GENERIC_TYPE_PRIMITIVE:
      if (PT_IS_PRIMITIVE_TYPE (op_type))
	{
	  return true;
	}
      break;
    case PT_GENERIC_TYPE_DISCRETE_NUMBER:
      if (PT_IS_DISCRETE_NUMBER_TYPE (op_type) || op_type == PT_TYPE_ENUMERATION)
	{
	  /* PT_GENERIC_TYPE_DISCRETE_NUMBER is equivalent with SHORT, INTEGER and BIGINT */
	  return true;
	}
      break;

    case PT_GENERIC_TYPE_NUMBER:
      if (PT_IS_NUMERIC_TYPE (op_type) || op_type == PT_TYPE_ENUMERATION)
	{
	  /* any NUMBER type is equivalent with PT_GENERIC_TYPE_NUMBER */
	  return true;
	}
      break;

    case PT_GENERIC_TYPE_STRING:
      if (PT_IS_CHAR_STRING_TYPE (op_type) || op_type == PT_TYPE_ENUMERATION)
	{
	  /* any STRING type is equivalent with PT_GENERIC_TYPE_STRING */
	  return true;
	}
      break;

    case PT_GENERIC_TYPE_CHAR:
      if (op_type == PT_TYPE_CHAR || op_type == PT_TYPE_VARCHAR || op_type == PT_TYPE_ENUMERATION)
	{
	  /* CHAR and VARCHAR are equivalent to PT_GENERIC_TYPE_CHAR */
	  return true;
	}
      break;

    case PT_GENERIC_TYPE_NCHAR:
      if (op_type == PT_TYPE_NCHAR || op_type == PT_TYPE_VARNCHAR)
	{
	  /* NCHAR and VARNCHAR are equivalent to PT_GENERIC_TYPE_NCHAR */
	  return true;
	}
      break;

    case PT_GENERIC_TYPE_BIT:
      if (PT_IS_BIT_STRING_TYPE (op_type))
	{
	  /* BIT and BIT VARYING are equivalent to PT_GENERIC_TYPE_BIT */
	  return true;
	}
      break;

    case PT_GENERIC_TYPE_DATETIME:
      if (PT_IS_DATE_TIME_TYPE (op_type))
	{
	  return true;
	}
      break;
    case PT_GENERIC_TYPE_DATE:
      if (PT_HAS_DATE_PART (op_type))
	{
	  return true;
	}
      break;
    case PT_GENERIC_TYPE_SEQUENCE:
      if (PT_IS_COLLECTION_TYPE (op_type))
	{
	  /* any COLLECTION is equivalent with PT_GENERIC_TYPE_SEQUENCE */
	  return true;
	}
      break;

    case PT_GENERIC_TYPE_JSON_VAL:
      return pt_is_json_value_type (op_type);

    case PT_GENERIC_TYPE_JSON_DOC:
      return pt_is_json_doc_type (op_type);

    case PT_GENERIC_TYPE_SCALAR:
      return ((op_type == PT_TYPE_ENUMERATION) || PT_IS_NUMERIC_TYPE (op_type) || PT_IS_STRING_TYPE (op_type)
	      || PT_IS_DATE_TIME_TYPE (op_type));

    default:
      return false;
    }

  return false;
}

/*
 * pt_get_equivalent_type () - get the type to which a node should be
 *			       converted to in order to match an expression
 *			       definition
 *   return	  : the new type
 *   def_type(in) : the type defined in the expression signature
 *   arg_type(in) : the type of the received expression argument
 */
PT_TYPE_ENUM
pt_get_equivalent_type (const PT_ARG_TYPE def_type, const PT_TYPE_ENUM arg_type)
{
  if (arg_type == PT_TYPE_NULL || arg_type == PT_TYPE_NONE)
    {
      /* either the argument is null or not defined */
      return arg_type;
    }

  if (def_type.type != pt_arg_type::GENERIC)
    {
      /* if the definition does not have a generic type, return the definition type */
      return def_type.val.type;
    }

  /* In some cases that involve ENUM (e.g. bit_length function) we need to convert ENUM to the other type even if the
   * types are equivalent */
  if (pt_are_equivalent_types (def_type, arg_type) && arg_type != PT_TYPE_ENUMERATION)
    {
      /* def_type includes type */
      if (arg_type == PT_TYPE_LOGICAL)
	{
	  /* def_type is a generic type and even though logical type might be equivalent with the generic definition,
	   * we are sure that we don't want it to be logical here */
	  return PT_TYPE_INTEGER;
	}
      return arg_type;
    }

  /* At this point we do not have a clear match. We will return the "largest" type for the generic type defined in the
   * expression signature */
  switch (def_type.val.generic_type)
    {
    case PT_GENERIC_TYPE_ANY:
      if (arg_type == PT_TYPE_LOGICAL)
	{
	  /* if PT_TYPE_LOGICAL appears for a PT_GENERIC_TYPE_ANY, it should be converted to PT_TYPE_INTEGER. */
	  return PT_TYPE_INTEGER;
	}
      return arg_type;

    case PT_GENERIC_TYPE_PRIMITIVE:
      if (PT_IS_PRIMITIVE_TYPE (arg_type))
	{
	  return arg_type;
	}
      break;

    case PT_GENERIC_TYPE_LOB:
      if (PT_IS_LOB_TYPE (arg_type))
	{
	  return arg_type;
	}
      break;

    case PT_GENERIC_TYPE_DISCRETE_NUMBER:
      return PT_TYPE_BIGINT;

    case PT_GENERIC_TYPE_NUMBER:
      if (arg_type == PT_TYPE_ENUMERATION)
	{
	  return PT_TYPE_SMALLINT;
	}
      return PT_TYPE_DOUBLE;

    case PT_GENERIC_TYPE_CHAR:
    case PT_GENERIC_TYPE_STRING:
    case PT_GENERIC_TYPE_STRING_VARYING:
      return PT_TYPE_VARCHAR;

    case PT_GENERIC_TYPE_NCHAR:
      return PT_TYPE_VARNCHAR;

    case PT_GENERIC_TYPE_BIT:
      return PT_TYPE_VARBIT;

    case PT_GENERIC_TYPE_DATE:
      return PT_TYPE_DATETIME;

    case PT_GENERIC_TYPE_SCALAR:
      if (arg_type == PT_TYPE_ENUMERATION || arg_type == PT_TYPE_MAYBE || PT_IS_NUMERIC_TYPE (arg_type)
	  || PT_IS_STRING_TYPE (arg_type) || PT_IS_DATE_TIME_TYPE (arg_type))
	{
	  return arg_type;
	}
      else
	{
	  return PT_TYPE_NONE;
	}

    case PT_GENERIC_TYPE_JSON_VAL:
      if (pt_is_json_value_type (arg_type))
	{
	  return arg_type;
	}
      else if (PT_IS_NUMERIC_TYPE (arg_type))
	{
	  return PT_TYPE_JSON;
	}
      else
	{
	  return PT_TYPE_NONE;
	}

    case PT_GENERIC_TYPE_JSON_DOC:
      if (pt_is_json_doc_type (arg_type))
	{
	  return arg_type;
	}
      else
	{
	  return PT_TYPE_NONE;
	}

    default:
      return PT_TYPE_NONE;
    }

  return PT_TYPE_NONE;
}
