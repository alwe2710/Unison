#pragma once

#include <borealis.hpp>

// Pushed from SettingsActivity's own "Info" cell -- app name, dynamic
// version (UNISON_APP_VERSION, see CMakeLists.txt), license, repo link.
// Plain static DetailCells, no selection/click behavior unlike
// LanguageActivity -- nothing here is editable.
class InfoActivity : public brls::Activity {
  public:
    brls::View *createContentView() override;
};
