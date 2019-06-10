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
 * cublocale.hpp
 */

#ifndef _CUBLOCALE_HPP_
#define _CUBLOCALE_HPP_

#include "language_support.h"
#include "intl_support.h"

#include <locale>
#include <codecvt>
#include <string>

#ident "$Id$"

namespace cublocale
{
  // utility wrapper to adapt locale-bound facets for wstring/wbuffer convert
  template<class Facet>
  struct deletable_facet : Facet
  {
      using Facet::Facet; // inherit constructors
      ~deletable_facet() {}
  };

  std::string get_locale_name(const LANG_COLLATION * lang_coll)
  {
    INTL_CODESET codeset = lang_coll->codeset;
    const char* codeset_name = lang_get_codeset_name(codeset);
    const char* lang_name = lang_coll->default_lang->lang_name;

    std::string locale_str;
    switch (codeset)
    {
      case INTL_CODESET_ISO88591:
      case INTL_CODESET_UTF8:
      case INTL_CODESET_KSC5601_EUC:
        locale_str = lang_name;
        locale_str += ".";
        locale_str += codeset_name;
        break;
    }
    return locale_str;
  }

  std::locale get_locale(const LANG_COLLATION * lang_coll)
  {
    std::string locale_str = get_locale_name (lang_coll);
    try
    {
      std::locale loc (locale_str.c_str());
      return loc;
    }
    catch (std::exception &e)
    {
      // return default locale, locale name is not supported
      return std::locale ();
    }
  }

  std::wstring convert_to_wstring (const std::string &src, const LANG_COLLATION * lang_coll)
  {
    INTL_CODESET codeset = lang_coll->codeset;
    typedef deletable_facet<std::codecvt_byname<wchar_t, char, std::mbstate_t>> codecvt_t;
    std::wstring wstr;
    switch (codeset)
    {
      case INTL_CODESET_UTF8:
      case INTL_CODESET_ISO88591:
        {
        std::string loc_str = get_locale_name (lang_coll);
        std::wstring_convert <codecvt_t> wstring_conv (new codecvt_t(loc_str));
        wstr = wstring_conv.from_bytes(src);
        }
        break;
      case INTL_CODESET_KSC5601_EUC:
        {
        int conv_size = 0;

        std::string u8str;
        u8str.reserve(src.size());
        const char* u8str_p = u8str.data();
        intl_euckr_to_utf8((const unsigned char*)src.data(), src.size(), (unsigned char**)&u8str_p, &conv_size);

        std::string loc_str = get_locale_name (lang_coll);
        std::wstring_convert <codecvt_t> wstring_conv (new codecvt_t(loc_str));
        wstr = wstring_conv.from_bytes(u8str);
        /*
        std::mbstate_t state = std::mbstate_t();
        const char *psrc = src.c_str();
        std::size_t len = 1 + std::mbsrtowcs(NULL, &psrc, 0, &state);
        wstr.reserve(len);
        std::mbsrtowcs(&wstr[0], &psrc, wstr.size(), &state);
        */
        }
        break;
      default:
        {
        wstr.assign(src.begin(), src.end());
        }
        break;
    }
    return wstr;
  }

  /*
  typedef deletable_facet<std::codecvt_byname<wchar_t, char, std::mbstate_t>> CVT;
  CVT* get_codecvt_facet (const LANG_COLLATION * lang_coll)
  {
    std::locale loc = get_locale(lang_coll);
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
  */
}

#endif /* _CUBLOCALE_HPP_ */