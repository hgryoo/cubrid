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
#include <locale>
#include <string>
#include <functional>

namespace cublang
{

  class cub_ctype : public std::ctype<char>
  {
    public:
      cub_ctype (int collation_id);

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

  /*
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
      LANG_COLLATION *lang_coll {nullptr};
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

  class collation_helper
  {
    public:
      static ALPHABET_DATA* user_alphabet(int coll_id);
  };

}

namespace cublang
{
  
  // collation_helper
  ALPHABET_DATA* collation_helper::user_alphabet(int coll_id)
  {
    return &(lang_get_collation(coll_id)->default_lang->alphabet);
  }

  //
  // common functions
  //
  std::string get_standard_name(const LANG_COLLATION * lang_coll)
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
    std::string locale_str = get_standard_name (lang_coll);
    std::locale loc (locale_str.c_str());
    //std::locale loc_w_facet (loc, new cub_collate(lang_coll->coll.coll_id));

    return loc;
  }
  
  //
  // cub_ctype
  //
  /*
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
    return c;
  }

  const char *
  cub_ctype::do_tolower (char *beg, const char *end) const
  {
    unsigned char *src, *result;
    const ALPHABET_DATA *alphabet = collation_helper::user_alphabet(coll_id)
    INTL_CODESET codeset = (lang_get_collation(coll_id)->codeset);
    
    src = (unsigned char *) beg;    
    size_t size = (end - beg) / sizeof(char);

    size_t intl_length = size;
    size_t lower_size;
    intl_char_count (src, size, codeset, &intl_length);
    lower_size = intl_lower_string_size (alphabet, intl_legnth, 
 
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
*/
  //
  // cub_collate
  //
/*
  cub_collate::cub_collate (int collation_id) : collate()
  {
    coll_id = collation_id;
    lang_coll = lang_get_collation (collation_id);
  }

  int
  cub_collate::do_compare (const char *low1, const char *high1,
			   const char *low2, const char *high2) const
  {
    size_t str1_size = (high1 - low1) / sizeof (char);
    size_t str2_size = (high2 - low2) / sizeof (char);

    int result = lang_coll->fastcmp (lang_coll, (unsigned char *) low1, str1_size, (unsigned char *) low2, str2_size);
    return result;
  }

  std::string
  cub_collate::do_transform (const char *low, const char *high) const
  {
    INTL_CODESET codeset = lang_coll->codeset;
    size_t byte_size = (high - low) / sizeof (char);
    std::string transformed;

    switch (codeset)
      {
      case INTL_CODESET_UTF8:
      {
	const unsigned char *str_begin, *str_end;
	unsigned char *str, *str_next;
	unsigned int cp, w_cp;
	const COLL_DATA *coll = & (lang_coll->coll);
	const int alpha_cnt = lang_coll->coll.w_count;
	const unsigned int *weight_ptr = lang_coll->coll.weights;


	str_begin = (unsigned char *) low;
	str = (unsigned char *) low;
	str_end = str + byte_size;
	while (str < str_end)
	  {
	    cp = intl_utf8_to_cp (str, CAST_BUFLEN (str_end - str), &str_next);

	    if (cp < (unsigned int) alpha_cnt)
	      {
		COLL_CONTRACTION *contr = NULL;
		if (str_end - str >= coll->contr_min_size &&
		    cp >= coll->cp_first_contr_offset &&
		    cp < (coll->cp_first_contr_offset + coll->cp_first_contr_count))
		  {
		    const int *first_contr = coll->cp_first_contr_array;
		    int contr_id = first_contr[cp - coll->cp_first_contr_offset];
		    int cmp;
		    if (contr_id != -1)
		      {
			contr = & (coll->contr_list[contr_id]);
			do
			  {
			    int str_size = CAST_BUFLEN (str_end - str);
			    if ((int) contr->size > str_size)
			      {
				cmp = memcmp (contr->c_buf, str, str_size);
				cmp = (cmp == 0) ? 1 : 0;
			      }
			    else
			      {
				cmp = memcmp (contr->c_buf, str, contr->size);

			      }

			    if (cmp >= 0)
			      {
				break;
			      }
			    contr++;
			    contr_id++;
			  }
			while (contr_id < coll->count_contr);
		      }
		    contr = (cmp == 0) ? contr : NULL;
		    w_cp = contr->wv;
		    str_next = (unsigned char *) str + contr->size;
		  }
		else
		  {
		    w_cp = weight_ptr[cp];
		  }
	      }
	    else
	      {
		w_cp = cp;
	      }
	    transformed.append ((char *) w_cp, sizeof (unsigned int));
	    str = str_next;
	  }
      }
      break;
      case INTL_CODESET_ISO88591:
      {
	transformed.assign (low, byte_size);
	for (int i = 0; i < byte_size; i++)
	  {
	    if (transformed[i] == ' ')
	      {
		transformed[i] = '\0';
	      }
	  }
      }
      break;
      case INTL_CODESET_KSC5601_EUC:
      {
	unsigned char *ptr = (unsigned char *) low;
	const unsigned char *str_end = ptr + byte_size;
#define EUC_SPACE 0xa1
#define ASCII_SPACE 0x20
#define ZERO '\0'

	unsigned char c;
	while (ptr < str_end)
	  {
	    c = *ptr++;
	    if (c == ASCII_SPACE)
	      {
		c = ZERO;
	      }
	    else if (c == EUC_SPACE && ptr < str_end && *ptr == EUC_SPACE)
	      {
		c = ZERO;
		ptr++;
	      }
	    transformed.push_back (c);
	  }
      }
      break;
      case INTL_CODESET_ASCII:
      case INTL_CODESET_BINARY:
      {
	transformed.assign (low, byte_size);
      }
      break;
      }
    return transformed;
  }

  long
  cub_collate::do_hash (const char *low, const char *high) const
  {
    size_t str_size = (high - low) / sizeof (char);
    unsigned int pseudo_key = lang_coll->mht2str (lang_coll, (unsigned char *) low, str_size);
    return pseudo_key;
  }

int utf8_uca_w_level (const COLL_DATA *coll_data, const int level, const unsigned char *str, const int size, int *offset_next_level)
{
  const unsigned char *str_end;
  const unsigned char *str_begin;
  unsigned char *str_next;

  UCA_L13_W *uca_w_l13 = NULL;
  UCA_L4_W *uca_w_l4 = NULL;
  int num_ce = 0;
  int ce_index = 0;
  unsigned int w1 = 0, w2 = 0;

  bool compute_offset = false;
  unsigned int str_cp_contr = 0;
  int cmp_offset = 0;

  assert (offset_next_level != NULL && *offset_next_level > -1);
  assert (level >= 0 && level <= 4);

  str_end = str + size;
  str_begin = str;

  if (level == 0)
  {
    assert (*offset_next_level == 0);
    compute_offset = true;
  }
  else
  {
    cmp_offset = *offset_next_level;
    if (cmp_offset > 0)
  {
      str += cmp_offset;
  }
      compute_offset = false;
  }
  str_next = (unsigned char *) str;

  while (1)
  {
    if (num_ce == 0)
    {
      str = str_next;
      if (str >= str_end)
      {
        break;
      }

      if (level == 3)
      {
        lang_get_uca_w_l4 (coll_data, true, str, CAST_BUFLEN (str_end - str), &uca_w_l4, &num_ce, &str_next, &str_cp_contr);
      }
      else
      {
        lang_get_uca_w_l13 (coll_data, true, str, CAST_BUFLEN (str_end - str), &uca_w_l13, &num_ce, &str_next, &str_cp_contr);

      }
      assert (num_ce > 0);
      ce_index = 0;
    }

    if (compute_offset)
    {
      if (ce_index == 0)
        {
          if (!INTL_CONTR_FOUND (str_cp_contr))
        {
          cmp_offset += CAST_BUFLEN (str_next - str);
        }
          else
        {
          compute_offset = false;
        }
        }
    }
  }

  return 0;

}
*/
}


#endif /* _CUBRID_LOCALE_HPP_ */
