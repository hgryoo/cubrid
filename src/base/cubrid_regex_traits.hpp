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
 * cubrid_regex_traits.hpp
 */

#ident "$Id$"

#include "language_support.h"
#include <locale>
#include <string>
#include <functional>

namespace cublang {

  /*
  template typename<charT>
  class cub_locale_facet
  {
    public:
	  static std::locale::id id;
  };
  */

  class cub_collate : public std::collate<char>
  {
    public:
      cub_collate(int collation_id) : collate()
      {
    	coll_id = collation_id;
        coll = lang_get_collation(collation_id);
      }
    protected:
      virtual int do_compare (const char* low1, const char* high1
              const char* low2, const char* high2) const
      {
        size_t str1_size = (high1 - low1)/sizeof(char);
        size_t str2_size = (high2 - low2)/sizeof(char);

        int result = coll->fastcmp (coll, low1, str1_size, low2, str2_size);
        return result;
      }

      virtual std::string do_transform (const char* low, const char* high) const
      {
    	std::string transformed;
	    INTL_CODESET codeset = coll->codeset;

	    using namespace std::placeholders;
	    std::function<int ()> next_coll_seq = std::bind(coll->next_coll_seq, coll, _1, _2, _3, _4);

	    char_type* cur = low;
	    while(cur != high)
	    {
	  	  int char_size = 0;
		  unsigned char* next_char = intl_next_char(cur, codeset, &char_size);
		  transformed.push_back(next_char);
		  cur += char_size;
	    }

	    return transformed;
      }

      virtual long do_hash (const char* low, const char* high) const
      {
        size_t str_size = (high1 - low1)/sizeof(char);
        unsigned int pseudo_key = coll->mht2str (coll, low1, str1_size, low2, str2_size);
        return pseudo_key;
      }
    private:
      int coll_id;
      LANG_COLLATION *coll {nullptr};
  };

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

#ifndef _CUBRID_REGEX_TRAITS_HPP_
#define _CUBRID_REGEX_TRAITS_HPP_

#endif /* _CUBRID_REGEX_TRAITS_HPP_ */
