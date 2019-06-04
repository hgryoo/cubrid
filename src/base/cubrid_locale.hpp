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
#include <codecvt>
#include <string>
#include <functional>

#define EUC_SPACE 0xa1
#define ASCII_SPACE 0x20
#define ZERO '\0'

namespace cublang
{
  // utility wrapper to adapt locale-bound facets for wstring/wbuffer convert
  template<class Facet>
  struct deletable_facet : Facet
  {
      using Facet::Facet; // inherit constructors
      ~deletable_facet() {}
  };

  class cub_ctype : public std::ctype<wchar_t>
  {
    public:
      cub_ctype(int collation_id);

    protected:
      /*
      virtual bool do_is (mask m, wchar_t c);
      virtual const char *do_is (wchar_t *low, const wchar_t *high, mask *vec) const;

      virtual const char *do_scan_is ( mask m, const wchar_t *beg, const wchar_t *end) const;
      virtual const char *do_scan_not ( mask m, const wchar_t *beg, const wchar_t *end) const;
      */

      virtual wchar_t do_toupper (wchar_t c) const;
      virtual const wchar_t *do_toupper (wchar_t *beg, const wchar_t *end) const;

      virtual wchar_t do_tolower (wchar_t c) const;
      virtual const wchar_t *do_tolower (wchar_t *beg, const wchar_t *end) const;

      /*
      virtual char do_widen (char c) const;
      virtual const char *do_widen (wchar_t *low, const wchar_t *high, wchar_t *to) const;

      virtual char do_narrow (wchar_t c, wchar_t dflt) const;
      virtual const char *do_narrow (wchar_t *low, const wchar_t *high, wchar_t dflt, wchar_t *to) const;
      */

      LANG_COLLATION *lang_coll {nullptr};
  };

  class cub_collate : public std::collate<wchar_t>
  {
    public:
      cub_collate (int collation_id);
    protected:
      virtual int do_compare (const wchar_t *low1, const wchar_t *high1,
			      const wchar_t *low2, const wchar_t *high2) const;
      virtual std::wstring do_transform (const wchar_t *low, const wchar_t *high) const;
      virtual long do_hash (const wchar_t *low, const wchar_t *high) const;

      COLL_CONTRACTION * get_contr (const COLL_DATA &coll_data, const wchar_t cp) const;

      LANG_COLLATION *lang_coll {nullptr};
  };
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
      std::locale loc_w_collate (loc, new cub_collate(lang_coll->coll.coll_id));
      std::locale loc_w_ctype (loc_w_collate, new cub_ctype(lang_coll->coll.coll_id));
      return loc_w_ctype;
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

  //
  // cub_ctype 
  //
  cub_ctype::cub_ctype(int collation_id) : std::ctype<wchar_t>()
  {
    lang_coll = lang_get_collation (collation_id);
  }
  /*
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
  */

  wchar_t
  cub_ctype::do_toupper (wchar_t c) const
  {
    const ALPHABET_DATA *alphabet = lang_user_alphabet_w_coll (lang_coll->coll.coll_id);
    
    switch (alphabet->codeset)
    {
      case INTL_CODESET_RAW_BYTES:
        return c;
        break;

      case INTL_CODESET_ISO88591:
        return (wchar_t) char_toupper_iso8859 ((wchar_t) c);
        break;
      
      case INTL_CODESET_KSC5601_EUC:
        return (wchar_t) char_toupper ((wchar_t) c);
        break;

      case INTL_CODESET_UTF8:
        // from intl_char_toupper_utf8()
        unsigned int cp = (unsigned int) c;
        if (cp < (unsigned int) (alphabet->l_count))
        {
          if (alphabet->upper_multiplier == 1)
          {
            return (wchar_t) alphabet->upper_cp[cp];
          }
          else
          {
            int count = 0;

            assert (alphabet->upper_multiplier > 1 && alphabet->upper_multiplier <= INTL_CASING_EXPANSION_MULTIPLIER);
            
            const unsigned int *case_p = &(alphabet->upper_cp[cp * alphabet->upper_multiplier]);
            while((count < alphabet->upper_multiplier && *case_p != 0))
            {
              case_p++;
              count++;
            }
            return (wchar_t) *case_p;
          }
        }
        return c;
        break;
    }
  }

  const wchar_t *
  cub_ctype::do_toupper (wchar_t *beg, const wchar_t *end) const
  {
    std::wstring r;
    wchar_t *ptr = beg;
    while(ptr < end)
    {
      r.push_back(do_toupper(*ptr++));
    }
    return r.c_str();
  }

  wchar_t
  cub_ctype::do_tolower (wchar_t c) const
  {
    const ALPHABET_DATA *alphabet = lang_user_alphabet_w_coll (lang_coll->coll.coll_id);
    
    switch (alphabet->codeset)
    {
      case INTL_CODESET_RAW_BYTES:
        return c;
        break;

      case INTL_CODESET_ISO88591:
        return (wchar_t) char_tolower_iso8859 ((wchar_t) c);
        break;
      
      case INTL_CODESET_KSC5601_EUC:
        return (wchar_t) char_tolower ((wchar_t) c);
        break;

      case INTL_CODESET_UTF8:
        // from intl_char_tolower_utf8()
        unsigned int cp = (unsigned int) c;
        if (cp < (unsigned int) (alphabet->l_count))
        {
          if (alphabet->lower_multiplier == 1)
          {
            return (wchar_t) alphabet->lower_cp[cp];
          }
          else
          {
            int count = 0;

            assert (alphabet->lower_multiplier > 1 && alphabet->lower_multiplier <= INTL_CASING_EXPANSION_MULTIPLIER);
            
            const unsigned int *case_p = &(alphabet->lower_cp[cp * alphabet->lower_multiplier]);
            while((count < alphabet->lower_multiplier && *case_p != 0))
            {
              case_p++;
              count++;
            }
            return (wchar_t) *case_p;
          }
        }
        return c;
        break;
    }
  }

  const wchar_t *
  cub_ctype::do_tolower (wchar_t *beg, const wchar_t *end) const
  {
    std::wstring r;
    wchar_t *ptr = beg;
    while(ptr < end)
    {
      r.push_back(do_tolower(*ptr++));
    }
    return r.c_str();
  }

  /*
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
  cub_collate::cub_collate (int collation_id) : collate()
  {
    lang_coll = lang_get_collation (collation_id);
  }

  int
  cub_collate::do_compare (const wchar_t *low1, const wchar_t *high1,
			   const wchar_t *low2, const wchar_t *high2) const
  {
    INTL_CODESET codeset = lang_coll->codeset;

    size_t size1 = (high1 - low1) / sizeof(wchar_t);
    size_t size2 = (high2 - low2) / sizeof(wchar_t);
    std::wstring wstr1 (low1, size1);
    std::wstring wstr2 (low2, size2);

    if (codeset == INTL_CODESET_UTF8)
    {
      std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
      std::string u8str1 = conv.to_bytes(wstr1);
      std::string u8str2 = conv.to_bytes(wstr2);

      int result = lang_coll->fastcmp (lang_coll, (unsigned char *) u8str1.c_str(), u8str1.size(), (unsigned char *) u8str2.c_str(), u8str2.size());
      return result;
    }
    else
    {
      return std::collate<wchar_t>::do_compare(low1, high1, low2, high2);
    }
  }

  COLL_CONTRACTION*
  cub_collate::get_contr (const COLL_DATA &coll_data, const wchar_t cp) const
  {
    const int *first_contr;
    int contr_id;
    COLL_CONTRACTION *contr;
    int cmp;

    first_contr = coll_data.cp_first_contr_array;
    (first_contr != NULL);
    contr_id = first_contr[(unsigned int) cp - coll_data.cp_first_contr_offset];

    if (contr_id == -1)
    {
      return NULL;
    }

    assert (contr_id >= 0 && contr_id < coll_data.count_contr);
    contr = &(coll_data.contr_list[contr_id]);

    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    std::string u8str = conv.to_bytes(cp);
    size_t u8str_size = u8str.size();
    do
    {
      if ((int) contr->size > u8str_size)
      {
        cmp = memcmp (contr->c_buf, u8str.c_str(), u8str_size);
        if (cmp == 0)
          {
            cmp = 1;
          }
      }
          else
      {
        cmp = memcmp (contr->c_buf, u8str.c_str(), contr->size);
      }

      if (cmp >= 0)
      {
        break;
      }
      contr++;
      contr_id++;
    }
    while (contr_id < coll_data.count_contr);

    if (cmp != 0)
      {
        contr = NULL;
      }

    return contr;
  }

  std::wstring
  cub_collate::do_transform (const wchar_t *low, const wchar_t *high) const
  {
    INTL_CODESET codeset = lang_coll->codeset;

    size_t size = (high - low) / sizeof(wchar_t);
    std::wstring s (low, size);

    wchar_t *ptr = (wchar_t *)low;
    const wchar_t *end = high;
    
    std::wstring t;
    
    if (codeset == INTL_CODESET_UTF8)
    {
      const int alpha_cnt = lang_coll->coll.w_count;
      const unsigned int *weight_ptr = lang_coll->coll.weights;
      const COLL_DATA &coll = lang_coll->coll;
      if (coll.uca_exp_num > 1)
      {
        /* compare level 1 */
        t = s;
      }
      else if (coll.count_contr > 0)
      {
        while (ptr < end)
        {
          wchar_t cp = *ptr;
          if (cp < (wchar_t) alpha_cnt)
          {
            COLL_CONTRACTION *contr = NULL;
            if (cp >= coll.cp_first_contr_offset 
                 && cp < (coll.cp_first_contr_offset + coll.cp_first_contr_count)
                 && (contr = this->get_contr(coll, (const wchar_t) cp)) != NULL)
            {
              assert (contr != NULL);
              unsigned int w_cp = contr->wv;
              t.push_back((wchar_t) w_cp);
            }
            else
            {
              t.push_back((wchar_t) weight_ptr[cp]);
            }
          }
          else
          {
            t.push_back(cp);
          }
          ptr++;
        }
      }
      else
      {
        while (ptr < end)
        {
          wchar_t cp = *ptr;
          if (cp < (wchar_t) alpha_cnt)
          {
            t.push_back((wchar_t) weight_ptr[cp]);
          }
          else
          {
            t.push_back(cp);
          }
          ptr++;
        }
      }
    }
    else if (codeset == INTL_CODESET_KSC5601_EUC)
    {
      while (ptr < end)
      {
        if (*ptr == ASCII_SPACE || *ptr == EUC_SPACE)
        {
          t.push_back((wchar_t) ZERO);
        }
        else
        {
          t.push_back(*ptr);
        }
        ptr++;
      }
    }
    else if (codeset == INTL_CODESET_ISO88591)
    {
#define PAD ' '			/* str_pad_char(INTL_CODESET_ISO88591, pad, &pad_size) */
      while (ptr < end)
      {
        if (*ptr == PAD)
        {
          t.push_back((wchar_t) ZERO);
        }
        else
        {
          t.push_back(*ptr);
        }
        ptr++;
      }
#undef PAD
    }
    else
    {
      t = s;
    }

    return t;
  }

  long
  cub_collate::do_hash (const wchar_t *low, const wchar_t *high) const
  {
    size_t size = (high - low) / sizeof(wchar_t);
    std::wstring wstr (low, size);

    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    std::string u8str = conv.to_bytes(wstr);
    size_t u8str_size = u8str.size();
    
    unsigned int pseudo_key = lang_coll->mht2str (lang_coll, (unsigned char *) u8str.c_str(), u8str_size);
    return pseudo_key;
  }

}

#endif /* _CUBRID_LOCALE_HPP_ */
