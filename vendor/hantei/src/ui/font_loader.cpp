#include "ui/font_loader.h"

#include "varswap/settings_state.h"
#include <imgui_internal.h>
#include <windows.h>
#include <cstdio>
#include <cstring>

#include <res/resource.h>

namespace {
ImVector<ImWchar> g_fontGlyphRanges;
}

void LoadJapaneseFonts(ImGuiIO& io)
{
    char winFolder[512]{};
    ImFontConfig config;

    if(g_fontGlyphRanges.empty()) {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());

        static const ImWchar symbol_ranges[] = {
            0x2000, 0x206F,
            0x2190, 0x21FF,
            0x25A0, 0x25FF,
            0x2600, 0x26FF,
            0x3000, 0x303F,
            0xFF00, 0xFFEF,
            0,
        };
        builder.AddRanges(symbol_ranges);

        builder.AddChar(0x203B);
        builder.AddChar(0x2190);
        builder.AddChar(0x2191);
        builder.AddChar(0x2192);
        builder.AddChar(0x2193);
        builder.AddChar(0x2605);
        builder.AddChar(0x2606);
        builder.AddChar(0x25CF);
        builder.AddChar(0x25CB);
        builder.AddChar(0x25A0);
        builder.AddChar(0x25A1);
        builder.AddChar(0x25C6);
        builder.AddChar(0x25C7);
        builder.AddChar(0x25B2);
        builder.AddChar(0x25B3);
        builder.AddChar(0x25BC);
        builder.AddChar(0x25BD);
        builder.AddChar(0x3000);

        builder.AddChar(0xFF00);
        builder.AddChar(0xFF01);
        builder.AddChar(0xFF08);
        builder.AddChar(0xFF09);
        builder.AddChar(0xFF0B);
        builder.AddChar(0xFF0D);
        builder.AddChar(0xFF10);
        builder.AddChar(0xFF11);
        builder.AddChar(0xFF12);
        builder.AddChar(0xFF13);
        builder.AddChar(0xFF14);
        builder.AddChar(0xFF15);
        builder.AddChar(0xFF16);
        builder.AddChar(0xFF17);
        builder.AddChar(0xFF18);
        builder.AddChar(0xFF19);
        builder.AddChar(0xFF1F);
        builder.AddChar(0xFF20);
        builder.AddChar(0xFF21);
        builder.AddChar(0xFF22);
        builder.AddChar(0xFF23);
        builder.AddChar(0xFF24);
        builder.AddChar(0xFF25);
        builder.AddChar(0xFF26);
        builder.AddChar(0xFF2A);
        builder.AddChar(0xFF33);
        builder.AddChar(0xFF38);

        builder.BuildRanges(&g_fontGlyphRanges);
    }

    const ImWchar* rangesToUse = g_fontGlyphRanges.empty() ? io.Fonts->GetGlyphRangesJapanese() : g_fontGlyphRanges.Data;

    ImFont* japaneseFont = nullptr;
    HMODULE hModule = GetModuleHandle(nullptr);
    if(hModule) {
        HRSRC res = FindResource(hModule, MAKEINTRESOURCE(NOTO_SANS_JP_F), RT_RCDATA);
        if(res) {
            HGLOBAL hRes = LoadResource(hModule, res);
            if(hRes) {
                void *notoFont = LockResource(hRes);
                if(notoFont) {
                    config.FontDataOwnedByAtlas = false;
                    japaneseFont = io.Fonts->AddFontFromMemoryTTF(notoFont, SizeofResource(hModule, res), gSettings.fontSize, &config, rangesToUse);
                    if(japaneseFont) {
                        printf("[Font] Noto Sans JP loaded with symbol support\n");
                    }
                }
            }
        }
    }

    if(!japaneseFont) {
        int appendAt = GetWindowsDirectoryA(winFolder, 512);
        strcpy(winFolder+appendAt, "\\Fonts\\meiryo.ttc");
        japaneseFont = io.Fonts->AddFontFromFileTTF(winFolder, gSettings.fontSize, &config, rangesToUse);
        if(japaneseFont) {
            printf("[Font] Meiryo loaded with symbol support (Noto not available)\n");

            if(hModule) {
                HRSRC res = FindResource(hModule, MAKEINTRESOURCE(NOTO_SANS_JP_F), RT_RCDATA);
                if(res) {
                    HGLOBAL hRes = LoadResource(hModule, res);
                    if(hRes) {
                        void *notoFont = LockResource(hRes);
                        if(notoFont) {
                            ImFontConfig mergeConfig;
                            mergeConfig.MergeMode = true;
                            mergeConfig.FontDataOwnedByAtlas = false;
                            io.Fonts->AddFontFromMemoryTTF(notoFont, SizeofResource(hModule, res), gSettings.fontSize, &mergeConfig, rangesToUse);
                            printf("[Font] Noto merged into Meiryo for missing glyphs\n");
                        }
                    }
                }
            }
        }
    }

    if(japaneseFont) {
        io.FontDefault = japaneseFont;
        printf("[Font] Japanese font loaded with symbol support and set as default\n");
        printf("[Font] Glyph ranges size: %d\n", g_fontGlyphRanges.Size);
        if(g_fontGlyphRanges.Size > 0) {
            printf("[Font] First few glyph ranges: ");
            for(int i = 0; i < g_fontGlyphRanges.Size && i < 20; i++) {
                printf("0x%04X ", g_fontGlyphRanges[i]);
            }
            printf("\n");
        }
    } else {
        printf("[Font] WARNING: Failed to load Japanese font! Japanese characters may not display correctly.\n");
    }
}
