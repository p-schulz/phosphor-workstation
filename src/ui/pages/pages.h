#pragma once

#include <memory>

#include "ui/page.h"

namespace phos::ui {

std::unique_ptr<Page> make_index_page();    // Page 1
std::unique_ptr<Page> make_setup_page();    // Page S (display section in M0)
std::unique_ptr<Page> make_help_page();     // Page H (stub)
std::unique_ptr<Page> make_testcard_page(); // TESTCARD overlay

} // namespace phos::ui
