#include "core/version.h"

namespace phos {

std::string_view version_string() { return PHOS_VERSION_STRING; }

std::string_view product_name() { return "PHOSPHOR"; }

} // namespace phos
