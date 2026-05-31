/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/string.h"

#include <string.h>
#include <algorithm>
#include <locale>

#include "xenia/base/platform.h"

#if !XE_PLATFORM_WIN32
#include <strings.h>
#endif  // !XE_PLATFORM_WIN32

#define UTF_CPP_CPLUSPLUS 201703L
#include "third_party/utfcpp/source/utf8.h"

namespace utfcpp = utf8;

namespace xe {

int xe_strcasecmp(const char* string1, const char* string2) {
#if XE_PLATFORM_WIN32
  return _stricmp(string1, string2);
#else
  return strcasecmp(string1, string2);
#endif  // XE_PLATFORM_WIN32
}

int xe_strncasecmp(const char* string1, const char* string2, size_t count) {
#if XE_PLATFORM_WIN32
  return _strnicmp(string1, string2, count);
#else
  return strncasecmp(string1, string2, count);
#endif  // XE_PLATFORM_WIN32
}

char* xe_strdup(const char* source) {
#if XE_PLATFORM_WIN32
  return _strdup(source);
#else
  return strdup(source);
#endif  // XE_PLATFORM_WIN32
}

std::string to_utf8(const std::u16string_view source) {
  try {
    return utfcpp::utf16to8(source);
  } catch (const utfcpp::exception&) {
    // Guest-provided strings can contain invalid UTF-16 (e.g. unpaired
    // surrogates), which would otherwise throw and take down callers such as
    // kernel argument logging. Transcode leniently, substituting U+FFFD for
    // anything malformed, so this conversion never throws. See issue #1780.
    std::string result;
    result.reserve(source.size());
    auto emit = [&result](char32_t cp) {
      if (cp <= 0x7F) {
        result.push_back(static_cast<char>(cp));
      } else if (cp <= 0x7FF) {
        result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else if (cp <= 0xFFFF) {
        result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else {
        result.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      }
    };
    for (size_t i = 0; i < source.size();) {
      char32_t cp = source[i++];
      if (cp >= 0xD800 && cp <= 0xDBFF) {
        // High surrogate: must be followed by a low surrogate.
        if (i < source.size() && source[i] >= 0xDC00 && source[i] <= 0xDFFF) {
          cp = 0x10000 + ((cp - 0xD800) << 10) + (source[i] - 0xDC00);
          ++i;
        } else {
          cp = 0xFFFD;
        }
      } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
        cp = 0xFFFD;  // unpaired low surrogate
      }
      emit(cp);
    }
    return result;
  }
}

std::u16string to_utf16(const std::string_view source) {
  return utfcpp::utf8to16(source);
}

}  // namespace xe
