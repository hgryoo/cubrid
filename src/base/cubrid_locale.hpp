/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * cubrid_locale.hpp
 */

#ifndef _CUBRID_LOCALE_HPP_
#define _CUBRID_LOCALE_HPP_

#ident "$Id$"

#include "language_support.h"
#include "string_opfunc.h"
#include <locale>
#include <string>
#include <functional>

namespace cublang
{
  // utility wrapper to adapt locale-bound facets for wstring/wbuffer convert
  template<class Facet>
  struct deletable_facet : Facet
  {
      using Facet::Facet; // inherit constructors
      ~deletable_facet() {}
  };

/*
  class cub_ctype : public std::ctype<char>
  {
    public:
      cub_ctype(int collation_id);

    protected:
      virtual bool do_is (mask m, char c);
      virtual const char *do_is (char *low, const char *high, mask *vec) const;

      virtual const char *do_scan_is ( mask m, const char *beg, const char *end) const;
      virtual const char *do_scan_not ( mask m, const char *beg, const char *end) const;

      virtual char do_toupper (char c) const;
      virtual const char *do_toupper (char *beg, const char *end) const;

      virtual char do_tolower (char c) const;
      virtual const char *do_tolower (char *beg, const char *end) const;

      virtual char do_widen (char c) const;
      virtual const char *do_widen (char *low, const char *high, char *to) const;

      virtual char do_narrow (char c, char dflt) const;
      virtual const char *do_narrow (char *low, const char *high, char dflt, char *to) const;

      int coll_id;
  };

  class cub_collate : public std::collate<char>
  {
    public:
      cub_collate (int collation_id);
    protected:
      virtual int do_compare (const char *low1, const char *high1,
			      const char *low2, const char *high2) const;
      virtual std::string do_transform (const char *low, const char *high) const;
      virtual long do_hash (const char *low, const char *high) const;

      int coll_id;
      LANG_COLLATION *coll {nullptr};
  };
*/
  /*
    template typename<charT>
    class cub_regex_traits : public regex_traits<charT>
    {
    	  public:
  	  	  charT translate(char c) const;
  	  	  charT translate_nocase(char c) const;
  	  	  template< class ForwardIt >
  	  	  string_type transform( ForwardIt first, ForwardIt last) const;
    };
  */
}

namespace cublang
{
  // collation_helper
  ALPHABET_DATA* user_alphabet(int coll_id)
  {
    return &(lang_get_collation(coll_id)->default_lang->alphabet);
  }

  //
  // common functions
  //
  std::string get_locale_name(const LANG_COLLATION * lang_coll)
  {
    INTL_CODESET codeset = lang_coll->codeset;
    const char* codeset_name = lang_get_codeset_name(codeset);
    const char* lang_name = lang_coll->default_lang->lang_name;

    std::string locale_str = lang_name;
    locale_str += ".";
    locale_str += codeset_name;

    return locale_str;
  }

  std::locale get_standard_locale(const LANG_COLLATION * lang_coll)
  {
    std::string locale_str = get_locale_name (lang_coll);
    try
    {
      std::locale loc (locale_str.c_str());
      //std::locale loc_w_facet (loc, new cub_collate(lang_coll->coll.coll_id));
      return loc;
    }
    catch (std::exception &e)
    {
      return std::locale ();
    }
  }

  typedef deletable_facet<std::codecvt_byname<wchar_t, char, std::mbstate_t>> CVT;
  CVT* get_codecvt_facet (const LANG_COLLATION * lang_coll)
  {
    std::locale loc = get_standard_locale(lang_coll);
    std::string locale_str = get_locale_name(lang_coll);

    CVT *cvt = nullptr;
    if (locale_str.compare(loc.name()) == 0)
    {
      cvt = new CVT(locale_str);
    }
    else
    {
      cvt = new CVT("");
    }
    return cvt;
  }

  /*
  //
  // cub_ctype 
  //
  cub_ctype::cub_ctype(int collation_id) : std::ctype<char>()
  {
    coll_id = collation_id;
  }

  bool
  cub_ctype::do_is (mask m, char c)
  {
    return std::ctype<char>::do_is(m, c);
  }

  const char *
  cub_ctype::do_is (char *low, const char *high, mask *vec) const
  {
    return std::ctype<char>::do_is(low, high, vec);
  }

  const char *
  cub_ctype::do_scan_is ( mask m, const char *beg, const char *end) const
  {

  }

  const char *
  cub_ctype::do_scan_not ( mask m, const char *beg, const char *end) const
  {
  }

  char
  cub_ctype::do_toupper (char c) const
  {
    ALPHABET_DATA user_alphabet = lang_user_alphabet_w_coll(coll_id);
    
  }

  const char *
  cub_ctype::do_toupper (char *beg, const char *end) const
  {
    size_t str1_size = (high1 - low1) / sizeof (char);
    ALPHABET_DATA alphabet = lang_user_alphabet_w_coll(coll_id);
    INTL_CODESET codeset = lang_get_collation(coll_id)->codeset;
    int length = 1; //default
    int size = 1;
    intl_char_count (beg, size, codeset, &length);
    int upper_size = intl_upper_string_size (alphabet, (unsigned char *) db_get_string (beg), db_get_string_size (string),
                  src_length);
  
        upper_str = (unsigned char *) db_private_alloc (NULL, upper_size + 1);
        if (!upper_str)
      {
        error_status = ER_OUT_OF_VIRTUAL_MEMORY;
      }
        else
      {
        int upper_length = TP_FLOATING_PRECISION_VALUE;
        intl_upper_string (alphabet, (unsigned char *) db_get_string (string), upper_str, src_length);
  
        upper_str[upper_size] = 0;
        if (db_value_precision (string) != TP_FLOATING_PRECISION_VALUE)
          {
            intl_char_count (upper_str, upper_size, db_get_string_codeset (string), &upper_length);
          }
        qstr_make_typed_string (str_type, upper_string, upper_length, (char *) upper_str, upper_size,
                    db_get_string_codeset (string), db_get_string_collation (string));
        upper_string->need_clear = true;
      }
  }

  char
  cub_ctype::do_tolower (char c) const
  {

  }

  const char *
  cub_ctype::do_tolower (char *beg, const char *end) const
  {

  }

  char
  cub_ctype::do_towiden (char c) const
  {

  }

  const char *
  cub_ctype::do_towiden (char *beg, const char *end, char *dst) const
  {
  }
  char
  cub_ctype::do_tonarrow (char c, char dflt) const
  {

  }

  const char *
  cub_ctype::do_tonarrow (char *beg, const char *end, cat dflt, char *dst) const
  {
  }

  //
  // cub_collate
  //
  cub_collate::cub_collate (int collation_id) : collate()
  {
    coll_id = collation_id;
    coll = lang_get_collation (collation_id);
  }

  int
  cub_collate::do_compare (const char *low1, const char *high1,
			   const char *low2, const char *high2) const
  {
    size_t str1_size = (high1 - low1) / sizeof (char);
    size_t str2_size = (high2 - low2) / sizeof (char);

    int result = coll->fastcmp (coll, (unsigned char *) low1, str1_size, (unsigned char *) low2, str2_size);
    return result;
  }

  std::string
  cub_collate::do_transform (const char *low, const char *high) const
  {
    INTL_CODESET codeset = coll->codeset;

    std::string transformed;
    int size_byte;

    char *ptr = low;
    while (ptr < low)
    {
      coll->next_coll_seq (coll, (unsigned char *) transformed_char_p, transformed_size, next_char, &size_byte);
      ptr += size_byte;
    }

    size_t size = (high - low) / sizeof (char);
    int len = 0;
    intl_char_count (low, size, codeset, &len);

    std::string transformed (len);
    char *ptr = low;
    char *next = ptr;
    int dummy;
    for (int i = 0; i < len; i++)
      {
	INTL_NEXT_CHAR (next, ptr, codeset, &dummy);
	ptr = next;
      }

    LANG_LOCALE_DATA *locale_data = coll->default_lang;
    INTL_LANG lang_id = locale_data->lang_id;

    const LANG_LOCALE_DATA *specific_loc = lang_get_specific_locale (lang_id, codeset);

    assert (specific_loc->txt_conv != NULL);
    TEXT_CONVERSION *txt_conv = specific_loc->txt_conv;

    char *transformed_char_p;
    int transformed_size;

    size_t str_size = (high - low) / sizeof (char);
    txt_conv->text_to_utf8_func ((char *) low, str_size, &transformed_char_p, &transformed_size);

    unsigned char *next_char;
    int next_size;
    coll->next_coll_seq (coll, (unsigned char *) transformed_char_p, transformed_size, next_char, &next_size);

    std::string transformed ((char *) next_char, next_size);

    return transformed;
  }

  long
  cub_collate::do_hash (const char *low, const char *high) const
  {
    size_t str_size = (high - low) / sizeof (char);
    unsigned int pseudo_key = coll->mht2str (coll, (unsigned char *) low, str_size);
    return pseudo_key;
  }
  */

}

#endif /* _CUBRID_LOCALE_HPP_ */
