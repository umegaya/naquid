#include <string>

#include "base/i18n/i18n_constants.h"

namespace net {
extern const char* const kCharsetLatin1 = base::kCodepageLatin1;
bool ConvertToUtf8(const std::string& text, const char* charset,
                   std::string* output) {
  //because this stub function always fails, der::kTeletexString is not supported. 
  //TODO(iyatomi): re-evaluate der::kTeletexString is necessary. 
  output->clear();
  return false;
}
} //net

