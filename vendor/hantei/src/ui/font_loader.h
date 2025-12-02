#ifndef UI_FONT_LOADER_H_GUARD
#define UI_FONT_LOADER_H_GUARD

#include <imgui.h>

// Loads Japanese + special symbol glyphs into ImGui and sets the default font.
// Shared between the main editor and the standalone VarSwap workbench.
void LoadJapaneseFonts(ImGuiIO& io);

#endif /* UI_FONT_LOADER_H_GUARD */
