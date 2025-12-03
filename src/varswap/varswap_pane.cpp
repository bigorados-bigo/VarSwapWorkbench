#include "varswap/varswap_pane.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <cstdlib>

using namespace varswap;

namespace {

constexpr float kSummaryListWidth = 190.f;
constexpr int kFrameJumpOffset = 10000;

const ImVec4 kPendingColor(0.95f, 0.82f, 0.2f, 1.f);
const ImVec4 kAppliedColor(0.2f, 0.75f, 0.2f, 1.f);
const ImVec4 kErrorColor(0.9f, 0.3f, 0.3f, 1.f);

const char* kCompareModeLabels[] = {"Greater (>)", "Less (<)", "Equal (==)"};
const char* kChangeModeLabels[] = {"Set", "Add"};
const int kCond38ModeValues[] = {0, 1, 10, 11};
const char* kCond38ModeLabels[] = {
    "Set",
    "Add",
    "Set owner var",
    "Add owner var"
};

std::string formatVarLabel(int varId) {
    if (varId >= 0 && varId < 100) {
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "%02d", varId);
        return buffer;
    }
    return std::to_string(varId);
}

std::uint64_t makeSummaryKey(int varId, VarCategory category, bool isGlobal) {
    std::uint64_t categoryBits = static_cast<std::uint64_t>(static_cast<int>(category)) & 0x7FFFFFFF;
    std::uint64_t globalBit = isGlobal ? 1ull : 0ull;
    return (categoryBits << 33) | (globalBit << 32) |
           static_cast<std::uint32_t>(varId);
}

bool categoryUsesNumericVarId(VarCategory category) {
    return !(category == VarCategory::Assist || category == VarCategory::Dash);
}

enum SummaryColumnId {
    SummaryColumnVar = 0,
    SummaryColumnCategory = 1,
    SummaryColumnCount = 2
};

enum OccurrenceColumnId {
    OccColumnSelect = 0,
    OccColumnVar,
    OccColumnCategory,
    OccColumnPattern,
    OccColumnFrame,
    OccColumnNode,
    OccColumnParams,
    OccColumnRaw,
    OccColumnNewValue,
    OccColumnStatus
};
std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool containsInsensitive(const std::string& haystack, const std::string& needleLower) {
    if (needleLower.empty()) {
        return true;
    }
    std::string hayLower = haystack;
    std::transform(hayLower.begin(), hayLower.end(), hayLower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return hayLower.find(needleLower) != std::string::npos;
}

std::string formatSequenceLabel(const Sequence& seq, int seqIndex) {
    std::ostringstream oss;
    oss << std::setw(3) << std::setfill('0') << seqIndex;
    oss << std::setfill(' ');
    if (!seq.name.empty()) {
        oss << " " << seq.name;
    } else if (!seq.codeName.empty()) {
        oss << " " << seq.codeName;
    } else {
        oss << " (unnamed)";
    }
    return oss.str();
}

} // namespace

VarSwapPane::VarSwapPane(FrameData* data)
    : frameData(data) {
    for (int i = 0; i < static_cast<int>(VarCategory::Count); ++i) {
        categoryVisibility[i] = true;
    }
    refreshScan();
}

void VarSwapPane::ForceRescan() {
    refreshScan();
}

void VarSwapPane::Draw() {
    if (!isVisible) {
        return;
    }

    if (!frameData) {
        return;
    }

    ImGui::Begin("VarSwap Workbench", &isVisible);

    if (!frameData->m_loaded) {
        drawEmptyState();
        ImGui::End();
        return;
    }

    if (ImGui::Button("Refresh Scan")) {
        refreshScan();
    }
    ImGui::SameLine();
    bool pending = hasPendingEdits();
    ImGui::BeginDisabled(!pending);
    if (ImGui::Button("Apply Pending")) {
        applyPendingChanges();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Clear Pending")) {
        clearPendingEdits();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%s", infoLabel);

    ImGui::Separator();

    // Filter controls
    ImGui::TextUnformatted("Categories:");
    ImGui::PushID("category_filters");
    for (int i = 0; i < static_cast<int>(VarCategory::Count); ++i) {
        ImGui::PushID(i);
        ImGui::Checkbox(categoryLabel(static_cast<VarCategory>(i)), &categoryVisibility[i]);
        ImGui::PopID();
        if (i + 1 < static_cast<int>(VarCategory::Count)) {
            ImGui::SameLine();
        }
    }
    ImGui::PopID();

    ImGui::Checkbox("Pending only", &pendingOnly);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.f);
    ImGui::InputTextWithHint("##VarSwapSearch", "Pattern, node, or var", &searchText);
    ImGui::Checkbox("Projectile globals only", &projectileGlobalsOnly);
    ImGui::SameLine();
    ImGui::Checkbox("Pending List", &showPendingListWindow);

    if (occurrences.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("No variable references found in this file.");
        ImGui::End();
        return;
    }

    auto visible = buildVisibleIndexList();
    if (visible.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("No rows match the current filters.");
        ImGui::End();
        return;
    }

    ImGui::Separator();

    ImGui::BeginChild("VarSwapTable", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false);
    drawTable(visible);
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::End();

    ImGui::SetNextWindowSize(ImVec2(420.f, 520.f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Var Summary", nullptr, ImGuiWindowFlags_None);
    drawSummary(visible);
    ImGui::End();

    if (showPendingListWindow) {
        drawPendingListWindow();
    }
}

void VarSwapPane::refreshScan() {
    invalidateSummaryCache();
    if (!frameData || !frameData->m_loaded) {
        occurrences.clear();
        occurrenceMetadata.clear();
        rowStates.clear();
        summaryCache.clear();
        summaryCacheVisibleIndices.clear();
        std::snprintf(infoLabel, sizeof(infoLabel), "No file loaded");
        return;
    }

    occurrences = collectOccurrences(*frameData);
    ensureRowStateSize();
    rebuildOccurrenceMetadata();
    summaryCache.clear();
    summaryCacheVisibleIndices.clear();
    std::snprintf(infoLabel, sizeof(infoLabel), "%zu occurrence(s)", occurrences.size());
}

void VarSwapPane::ensureRowStateSize() {
    rowStates.assign(occurrences.size(), RowState{});
}

void VarSwapPane::drawEmptyState() {
    ImGui::TextDisabled("Load a HA6 file to inspect variables.");
}

std::vector<int> VarSwapPane::buildVisibleIndexList() {
    std::vector<int> result;
    result.reserve(occurrences.size());
    std::string needle = toLower(searchText);
    for (size_t i = 0; i < occurrences.size(); ++i) {
        if (matchesFilters(i, needle)) {
            result.push_back(static_cast<int>(i));
        }
    }
    return result;
}

bool VarSwapPane::matchesFilters(size_t index, const std::string& needleLower) {
    if (index >= occurrences.size()) {
        return false;
    }
    const auto& occ = occurrences[index];
    int categoryIndex = static_cast<int>(occ.category);
    if (categoryIndex < 0 || categoryIndex >= static_cast<int>(VarCategory::Count)) {
        categoryIndex = static_cast<int>(VarCategory::Unknown);
    }
    if (!categoryVisibility[categoryIndex]) {
        return false;
    }
    if (pendingOnly) {
        if (index >= rowStates.size() || !rowStates[index].hasPending()) {
            return false;
        }
    }
    const auto& meta = metaFor(index);
    if (projectileGlobalsOnly && !meta.isGlobalProjectile) {
        return false;
    }
    if (!needleLower.empty() && meta.searchLower.find(needleLower) == std::string::npos) {
        return false;
    }
    return true;
}

void VarSwapPane::drawSummary(const std::vector<int>& visibleIndices) {
    auto& entries = getSummaryEntries(visibleIndices);

    ImGui::Text("Variables (%zu)", entries.size());

    if (ImGui::BeginTable("VarSummarySplitTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("VarList", ImGuiTableColumnFlags_WidthFixed, kSummaryListWidth);
        ImGui::TableSetupColumn("VarDetail", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextColumn();
        ImGui::BeginChild("VarSummaryListPanel", ImVec2(0, 0), true);
        ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate;
        if (ImGui::BeginTable("VarSummaryTable", 3, flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Var", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 70.f, SummaryColumnVar);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.f, SummaryColumnCategory);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 60.f, SummaryColumnCount);
            ImGui::TableHeadersRow();

            std::vector<int> order(entries.size());
            std::iota(order.begin(), order.end(), 0);
            const ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
            sortSummaryEntries(order, entries, sortSpecs);
            if (sortSpecs) {
                const_cast<ImGuiTableSortSpecs*>(sortSpecs)->SpecsDirty = false;
            }

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(order.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    auto& entry = entries[order[row]];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    bool isActive = (selectedSummaryKey != 0 && selectedSummaryKey == entry.cacheKey);
                    bool isGlobalProjectile = entry.isProjectileGlobal;
                    if (isGlobalProjectile) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.25f, 1.f));
                    }
                    ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(entry.cacheKey ? entry.cacheKey : static_cast<std::uint64_t>(entry.varId))));
                    if (ImGui::Selectable(entry.label.c_str(), isActive, ImGuiSelectableFlags_SpanAllColumns)) {
                        selectedSummaryKey = entry.cacheKey;
                        selectedVarId = entry.hasNumericVarId ? entry.varId : std::numeric_limits<int>::min();
                        selectedCategory = entry.category;
                        if (entry.category == VarCategory::Projectile) {
                            selectedProjectileFlagValid = true;
                            selectedProjectileIsGlobal = entry.isProjectileGlobal;
                        } else {
                            selectedProjectileFlagValid = false;
                            selectedProjectileIsGlobal = false;
                        }
                        // Always force-detail refresh on click
                        lastGlobalSelection = std::numeric_limits<int>::min();
                        lastGlobalSelectionCategory = VarCategory::Count;
                    }
                    ImGui::PopID();
                    if (isGlobalProjectile) {
                        if (ImGui::IsItemHovered()) {
                            std::string desc = describeProjectileVar(entry.varId);
                            ImGui::SetTooltip("%s", desc.c_str());
                        }
                        ImGui::PopStyleColor();
                    }
                    ImGui::TableSetColumnIndex(1);
                    if (isGlobalProjectile) {
                        ImGui::Text("%s (G)", categoryLabel(entry.category));
                    } else {
                        ImGui::TextUnformatted(categoryLabel(entry.category));
                    }
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", entry.count);
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::TableNextColumn();
        ImGui::BeginChild("VarSummaryDetailPanel", ImVec2(0, 0), true);
        SummaryEntry* selectedEntry = findSummaryEntryByKey(selectedSummaryKey);
        drawVarDetailPanel(selectedEntry);
        ImGui::EndChild();

        ImGui::EndTable();
    }
}

std::vector<VarSwapPane::SummaryEntry> VarSwapPane::buildSummaryEntries(const std::vector<int>& visibleIndices) {
    std::unordered_map<std::uint64_t, SummaryEntry> summaryMap;
    summaryMap.reserve(visibleIndices.size());

    for (int row : visibleIndices) {
        const auto& occ = occurrences[row];
        const auto& meta = metaFor(static_cast<size_t>(row));
        int varId = currentVar(occ);
        bool hasNumericVarId = categoryUsesNumericVarId(occ.category);
        int summaryVarId = hasNumericVarId ? varId : 0;

        bool keyIsGlobal = (occ.category == VarCategory::Projectile) && meta.isGlobalProjectile;
        std::uint64_t key = makeSummaryKey(summaryVarId, occ.category, keyIsGlobal);
        auto& entry = summaryMap[key];
        entry.varId = summaryVarId;
        entry.hasNumericVarId = hasNumericVarId;
        entry.count++;
        entry.category = occ.category;
        if (occ.category == VarCategory::Projectile) {
            entry.isProjectileGlobal = entry.isProjectileGlobal || meta.isGlobalProjectile;
        }
        entry.patternCounts[occ.seqIndex]++;
        entry.patternsDirty = true;
        if (meta.isGlobalProjectile) {
            entry.globalOpCount++;
            if (meta.globalDecrement) {
                entry.globalDecreaseTotal += meta.globalDelta;
            } else {
                entry.globalIncreaseTotal += meta.globalDelta;
            }
        }
    }

    std::vector<SummaryEntry> entries;
    entries.reserve(summaryMap.size());
    for (auto& kv : summaryMap) {
        auto entry = std::move(kv.second);
        entry.cacheKey = kv.first;
        bool hasGlobalOps = entry.globalOpCount > 0;
        bool isGlobalRegister = (entry.category == VarCategory::Projectile) && isProjectileGlobal(entry.varId);
        entry.isProjectileGlobal = entry.category == VarCategory::Projectile && (entry.isProjectileGlobal || hasGlobalOps || isGlobalRegister);
        if (entry.hasNumericVarId) {
            entry.label = formatVarLabel(entry.varId);
        } else {
            entry.label = categoryLabel(entry.category);
        }
        entry.sortedPatterns.clear();
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<VarSwapPane::SummaryEntry>& VarSwapPane::getSummaryEntries(const std::vector<int>& visibleIndices) {
    if (summaryCacheDirty || summaryCacheVisibleIndices != visibleIndices) {
        summaryCache = buildSummaryEntries(visibleIndices);
        summaryCacheVisibleIndices = visibleIndices;
        summaryCacheDirty = false;
    }
    return summaryCache;
}

void VarSwapPane::invalidateSummaryCache() {
    summaryCacheDirty = true;
}

void VarSwapPane::sortSummaryEntries(std::vector<int>& order, const std::vector<SummaryEntry>& entries, const ImGuiTableSortSpecs* sortSpecs) {
    auto comparator = [&](int lhsIndex, int rhsIndex) {
        const auto& lhs = entries[lhsIndex];
        const auto& rhs = entries[rhsIndex];
        auto compareVar = [&]() {
            if (lhs.varId == rhs.varId) return 0;
            return lhs.varId < rhs.varId ? -1 : 1;
        };
        auto compareLabel = [&]() {
            if (lhs.label == rhs.label) return 0;
            return lhs.label < rhs.label ? -1 : 1;
        };

        if (!sortSpecs || !sortSpecs->SpecsCount) {
            return compareVar() < 0;
        }

        for (int n = 0; n < sortSpecs->SpecsCount; ++n) {
            const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[n];
            int delta = 0;
            switch (spec.ColumnUserID) {
                case SummaryColumnVar:
                    if (lhs.hasNumericVarId && rhs.hasNumericVarId) {
                        delta = (lhs.varId == rhs.varId) ? 0 : (lhs.varId < rhs.varId ? -1 : 1);
                    } else if (!lhs.hasNumericVarId && !rhs.hasNumericVarId) {
                        delta = compareLabel();
                    } else {
                        delta = lhs.hasNumericVarId ? -1 : 1;
                    }
                    break;
                case SummaryColumnCategory:
                    delta = static_cast<int>(lhs.category) - static_cast<int>(rhs.category);
                    if (delta == 0 && lhs.category == VarCategory::Projectile) {
                        if (lhs.isProjectileGlobal != rhs.isProjectileGlobal) {
                            delta = lhs.isProjectileGlobal ? -1 : 1;
                        }
                    }
                    break;
                case SummaryColumnCount:
                    delta = lhs.count - rhs.count;
                    break;
                default:
                    break;
            }
            if (delta != 0) {
                if (spec.SortDirection == ImGuiSortDirection_Descending) {
                    delta = -delta;
                }
                return delta < 0;
            }
        }
        return compareVar() < 0;
    };

    std::sort(order.begin(), order.end(), comparator);
}

void VarSwapPane::drawVarDetailPanel(SummaryEntry* entry) {
    if (!entry) {
        ImGui::TextDisabled("Select a variable to inspect patterns.");
        return;
    }

    if (entry->varId != lastGlobalSelection || entry->category != lastGlobalSelectionCategory) {
        globalReplaceInput.clear();
        globalReplaceStatus.clear();
        lastGlobalSelection = entry->varId;
        lastGlobalSelectionCategory = entry->category;
    }

    bool isGlobalProjectile = entry->isProjectileGlobal;
    if (entry->hasNumericVarId) {
        ImGui::Text("Var %d (%s)", entry->varId, categoryLabel(entry->category));
    } else {
        ImGui::Text("%s variable", categoryLabel(entry->category));
    }
    if (isGlobalProjectile) {
        ImGui::SameLine();
        std::string description = "Projectile global value (EF6 #100/#101)";
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.f), "%s", description.c_str());
    }
    ImGui::Text("Occurrences: %d", entry->count);
    if (entry->globalOpCount > 0) {
        ImGui::Text("Projectile globals: %d (+%d / -%d)", entry->globalOpCount, entry->globalIncreaseTotal, entry->globalDecreaseTotal);
    }

    drawGlobalReplaceControls(entry);

    ImGui::Separator();
    drawPatternList(entry);
}

void VarSwapPane::drawGlobalReplaceControls(const SummaryEntry* entry) {
    if (!entry) {
        return;
    }

    if (!entry->hasNumericVarId) {
        ImGui::TextDisabled("Global replace is not available for %s vars.", categoryLabel(entry->category));
        return;
    }

    ImGui::TextUnformatted("Replace every occurrence");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##GlobalReplaceInput", &globalReplaceInput, ImGuiInputTextFlags_CharsDecimal);

    bool canApply = entry && !globalReplaceInput.empty();
    ImGui::BeginDisabled(!canApply);
    if (ImGui::Button("Apply to Entire File")) {
        try {
            int newValue = std::stoi(globalReplaceInput);
            applyGlobalReplace(*entry, newValue);
        } catch (...) {
            globalReplaceStatus = "Invalid number.";
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Clear##GlobalReplaceInput")) {
        globalReplaceInput.clear();
        globalReplaceStatus.clear();
    }

    if (!globalReplaceStatus.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.f, 1.f), "%s", globalReplaceStatus.c_str());
    }
}

void VarSwapPane::drawPatternList(SummaryEntry* entry) {
    if (!entry) {
        ImGui::TextDisabled("No variable selected.");
        return;
    }

    if (entry->hasNumericVarId) {
        ImGui::Text("Patterns with var %d", entry->varId);
    } else {
        ImGui::Text("Patterns with %s", categoryLabel(entry->category));
    }
    if (entry->patternCounts.empty()) {
        ImGui::TextDisabled("No pattern references in view.");
        return;
    }

    const auto& patternList = ensureSortedPatterns(*entry);

    ImGui::BeginChild("VarPatternList", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& pattern : patternList) {
        std::string name = frameData ? frameData->GetDecoratedName(pattern.first) : std::to_string(pattern.first);
        ImGui::BulletText("%s (%d)", name.c_str(), pattern.second);
    }
    ImGui::EndChild();
}

VarSwapPane::SummaryEntry* VarSwapPane::findSummaryEntryByKey(std::uint64_t key) {
    if (key == 0) {
        return nullptr;
    }
    for (auto& entry : summaryCache) {
        if (entry.cacheKey == key) {
            return &entry;
        }
    }
    return nullptr;
}

const std::vector<std::pair<int, int>>& VarSwapPane::ensureSortedPatterns(SummaryEntry& entry) {
    if (!entry.patternsDirty) {
        return entry.sortedPatterns;
    }
    entry.sortedPatterns.assign(entry.patternCounts.begin(), entry.patternCounts.end());
    std::sort(entry.sortedPatterns.begin(), entry.sortedPatterns.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second == rhs.second) {
            return lhs.first < rhs.first;
        }
        return lhs.second > rhs.second;
    });
    entry.patternsDirty = false;
    return entry.sortedPatterns;
}

void VarSwapPane::rebuildOccurrenceMetadata() {
    occurrenceMetadata.clear();
    occurrenceMetadata.resize(occurrences.size());
    for (size_t i = 0; i < occurrences.size(); ++i) {
        const auto& occ = occurrences[i];
        auto& meta = occurrenceMetadata[i];
        meta.patternLabel = buildPatternLabel(occ);
        meta.nodeLabel = buildNodeLabel(occ);
            int varId = currentVar(occ);
            bool modifiesGlobalRegister = isProjectileGlobalOp(occ);
            meta.isGlobalProjectile = modifiesGlobalRegister;
            meta.globalDecrement = meta.isGlobalProjectile && (occ.kind == OccurrenceKind::EfType6No101);
            if (meta.isGlobalProjectile) {
                meta.globalVar = varId;
                meta.globalDelta = compositeRemainder(occ);
            } else {
                meta.globalVar = 0;
                meta.globalDelta = 0;
                meta.globalDecrement = false;
            }

            std::string searchBlob = meta.patternLabel;
            searchBlob.push_back(' ');
            searchBlob += meta.nodeLabel;
            searchBlob.push_back(' ');
            searchBlob += std::to_string(varId);
            if (meta.isGlobalProjectile) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "global v%d %c%02d", meta.globalVar, meta.globalDecrement ? '-' : '+', meta.globalDelta);
                searchBlob.push_back(' ');
                searchBlob += buf;
            }
            meta.searchLower = toLower(searchBlob);
    }
}

const VarSwapPane::OccurrenceMetadata& VarSwapPane::metaFor(size_t index) const {
    static OccurrenceMetadata empty{};
    if (index >= occurrenceMetadata.size()) {
        return empty;
    }
    return occurrenceMetadata[index];
}

void VarSwapPane::sortOccurrenceIndices(std::vector<int>& indices, const ImGuiTableSortSpecs* sortSpecs) {
    if (indices.empty() || !sortSpecs || sortSpecs->SpecsCount == 0) {
        return;
    }

    std::sort(indices.begin(), indices.end(), [&](int lhsIndex, int rhsIndex) {
        const auto& lhs = occurrences[lhsIndex];
        const auto& rhs = occurrences[rhsIndex];

        auto compareStrings = [](const std::string& a, const std::string& b) {
            if (a == b) {
                return 0;
            }
            return a < b ? -1 : 1;
        };

        for (int n = 0; n < sortSpecs->SpecsCount; ++n) {
            const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[n];
            int delta = 0;
            switch (spec.ColumnUserID) {
                case OccColumnVar:
                    delta = currentVar(lhs) - currentVar(rhs);
                    break;
                case OccColumnCategory:
                    delta = static_cast<int>(lhs.category) - static_cast<int>(rhs.category);
                    break;
                case OccColumnPattern:
                    delta = compareStrings(metaFor(lhsIndex).patternLabel, metaFor(rhsIndex).patternLabel);
                    break;
                case OccColumnFrame:
                    delta = lhs.frameIndex - rhs.frameIndex;
                    break;
                case OccColumnNode:
                    delta = compareStrings(metaFor(lhsIndex).nodeLabel, metaFor(rhsIndex).nodeLabel);
                    break;
                case OccColumnParams:
                    delta = 0;
                    break;
                case OccColumnRaw: {
                    int lhsVal = lhs.valuePtr ? *lhs.valuePtr : 0;
                    int rhsVal = rhs.valuePtr ? *rhs.valuePtr : 0;
                    delta = lhsVal - rhsVal;
                    break;
                }
                default:
                    break;
            }

            if (delta != 0) {
                if (spec.SortDirection == ImGuiSortDirection_Descending) {
                    delta = -delta;
                }
                return delta < 0;
            }
        }

        return lhsIndex < rhsIndex;
    });
}

void VarSwapPane::drawTable(const std::vector<int>& visibleIndices) {
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                           ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Sortable |
                           ImGuiTableFlags_ScrollX;
    constexpr int kColumnCount = 10;
    if (ImGui::BeginTable("VarSwapOccurrenceTable", kColumnCount, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Sel", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 35.f, OccColumnSelect);
        ImGui::TableSetupColumn("Var", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 55.f, OccColumnVar);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 80.f, OccColumnCategory);
        ImGui::TableSetupColumn("Pattern", ImGuiTableColumnFlags_NoHide, -1.f, OccColumnPattern);
        ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthFixed, 55.f, OccColumnFrame);
        ImGui::TableSetupColumn("Node", ImGuiTableColumnFlags_NoHide, -1.f, OccColumnNode);
        ImGui::TableSetupColumn("Params", ImGuiTableColumnFlags_WidthFixed, 180.f, OccColumnParams);
        ImGui::TableSetupColumn("Raw", ImGuiTableColumnFlags_WidthFixed, 65.f, OccColumnRaw);
        ImGui::TableSetupColumn("New Value", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 90.f, OccColumnNewValue);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 80.f, OccColumnStatus);
        ImGui::TableHeadersRow();

        std::vector<int> sortedIndices = visibleIndices;
        const ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
        sortOccurrenceIndices(sortedIndices, sortSpecs);
        if (sortSpecs) {
            const_cast<ImGuiTableSortSpecs*>(sortSpecs)->SpecsDirty = false;
        }

        SummaryEntry* selectedEntry = findSummaryEntryByKey(selectedSummaryKey);

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(sortedIndices.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                int index = sortedIndices[row];
                auto& occ = occurrences[index];
                RowState& state = rowStates[index];
                int varId = currentVar(occ);
                int rawValue = occ.valuePtr ? *occ.valuePtr : 0;
                const auto& meta = metaFor(index);
                bool highlight = false;
                if (selectedEntry) {
                    if (occ.category == selectedEntry->category) {
                        if (selectedEntry->hasNumericVarId) {
                            if (currentVar(occ) == selectedEntry->varId) {
                                if (selectedEntry->category == VarCategory::Projectile) {
                                    if (selectedEntry->isProjectileGlobal == meta.isGlobalProjectile) {
                                        highlight = true;
                                    }
                                } else {
                                    highlight = true;
                                }
                            }
                        } else {
                            highlight = true;
                        }
                    }
                }

                ImGui::TableNextRow();
                if (highlight) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImVec4(0.1f, 0.3f, 0.5f, 0.15f)));
                }
                if (meta.isGlobalProjectile) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.35f, 0.3f, 0.05f, 0.12f)));
                }

                ImGui::TableSetColumnIndex(0);
                std::string checkboxId = "##sel" + std::to_string(index);
                ImGui::Checkbox(checkboxId.c_str(), &state.selected);

                ImGui::TableSetColumnIndex(1);
                if (categoryUsesNumericVarId(occ.category)) {
                    ImGui::Text("%d", varId);
                } else {
                    ImGui::TextUnformatted(categoryLabel(occ.category));
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(categoryLabel(occ.category));

                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(meta.patternLabel.c_str());

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%d", occ.frameIndex);

                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(meta.nodeLabel.c_str());
                if (meta.isGlobalProjectile) {
                    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.8f, 0.3f, 1.f));
                    ImGui::Text("Global V%d %c%02d", meta.globalVar, meta.globalDecrement ? '-' : '+', meta.globalDelta);
                    ImGui::PopStyleColor();
                }

                ImGui::TableSetColumnIndex(6);
                drawParameterControls(index, occ, state);

                ImGui::TableSetColumnIndex(7);
                if (occ.encoding == ValueEncoding::TensComposite) {
                    ImGui::Text("%d (d=%d)", rawValue, compositeRemainder(occ));
                } else if (occ.encoding == ValueEncoding::HundredsComposite) {
                    char sign = (occ.kind == OccurrenceKind::EfType6No101) ? '-' : '+';
                    ImGui::Text("%d (%c%02d)", rawValue, sign, compositeRemainder(occ));
                } else {
                    ImGui::Text("%d", rawValue);
                }

                ImGui::TableSetColumnIndex(8);
                ImGui::PushID(index);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##edit", &state.newValue, ImGuiInputTextFlags_CharsDecimal)) {
                    state.status = RowState::Status::None;
                    state.statusMessage.clear();
                }
                ImGui::PopID();

                ImGui::TableSetColumnIndex(9);
                switch (state.status) {
                    case RowState::Status::Applied:
                        ImGui::TextColored(kAppliedColor, "OK");
                        break;
                    case RowState::Status::Error:
                        ImGui::TextColored(kErrorColor, "%s", state.statusMessage.c_str());
                        break;
                    default:
                        if (state.hasPending()) {
                            ImGui::TextColored(kPendingColor, "Pending");
                        } else {
                            ImGui::Dummy(ImVec2(0.f, 0.f));
                        }
                        break;
                }
            }
        }

        ImGui::EndTable();
    }
}

void VarSwapPane::drawPendingListWindow() {
    if (!ImGui::Begin("Pending Actions", &showPendingListWindow)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Apply pending edits before saving or closing.");
    ImGui::Separator();

    bool any = false;
    size_t count = std::min(rowStates.size(), occurrences.size());
    for (size_t i = 0; i < count; ++i) {
        if (!rowStates[i].hasPending()) {
            continue;
        }
        any = true;
        const auto& occ = occurrences[i];
        const auto& meta = metaFor(i);
        std::string desc = describePendingAction(i);
        ImGui::PushStyleColor(ImGuiCol_Text, kPendingColor);
        ImGui::Text("%03d | %s | %s", occ.seqIndex, meta.patternLabel.c_str(), categoryLabel(occ.category));
        ImGui::PopStyleColor();
        ImGui::TextDisabled("%s", desc.c_str());
        ImGui::Separator();
    }

    if (!any) {
        ImGui::TextDisabled("No pending edits.");
    }

    ImGui::End();
}

std::string VarSwapPane::describePendingAction(size_t index) const {
    if (index >= rowStates.size() || index >= occurrences.size()) {
        return "Pending change";
    }
    const RowState& state = rowStates[index];
    const auto& occ = occurrences[index];
    std::ostringstream oss;
    bool first = true;
    auto appendChange = [&](const std::string& label, const std::string& before, const std::string& after) {
        if (!first) {
            oss << ", ";
        }
        oss << label << ": " << before << " -> " << after;
        first = false;
    };

    auto toString = [](int value) {
        return std::to_string(value);
    };

    auto parseInt = [](const std::string& text, int fallback) {
        try {
            return std::stoi(text);
        } catch (...) {
            return fallback;
        }
    };

    bool recordedValueChange = false;
    if (!state.newValue.empty()) {
        int before = currentVar(occ);
        int afterValue = parseInt(state.newValue, before);
        appendChange("Value", toString(before), toString(afterValue));
        recordedValueChange = true;
    }
    if (state.amountPending) {
        int before = currentVar(occ);
        appendChange(recordedValueChange ? "Amount" : "Value", toString(before), toString(state.amountValue));
        recordedValueChange = true;
    }
    if (state.deltaPending) {
        int before = currentDeltaValue(occ);
        appendChange("Delta", toString(before), toString(state.deltaValue));
    }
    if (state.compareValuePending && occ.compareValuePtr) {
        appendChange("Compare value", toString(*occ.compareValuePtr), toString(state.compareValue));
    }
    if (state.compareModePending && occ.compareModePtr) {
        appendChange("Compare mode", toString(*occ.compareModePtr), toString(state.compareMode));
    }
    if (state.changeValuePending && occ.changeValuePtr) {
        appendChange("Change value", toString(*occ.changeValuePtr), toString(state.changeValue));
    }
    if (state.changeModePending && occ.changeModePtr) {
        appendChange("Change mode", toString(*occ.changeModePtr), toString(state.changeMode));
    }
    if (state.jumpTargetPending) {
        int beforeValue = 0;
        bool beforeIsFrame = true;
        decodeJumpTarget(occ, beforeValue, beforeIsFrame);
        std::string before = beforeIsFrame ? ("Frame " + toString(beforeValue)) : ("Pattern " + toString(beforeValue));
        std::string after = (state.jumpTargetIsFrame || !occ.jumpTargetSupportsPattern)
            ? ("Frame " + toString(state.jumpTargetValue))
            : ("Pattern " + toString(state.jumpTargetValue));
        appendChange("Jump", before, after);
    }

    if (first) {
        return "Pending change";
    }
    return oss.str();
}

void VarSwapPane::applyPendingChanges() {
    if (!frameData) {
        return;
    }

    bool appliedAny = false;
    std::set<int> touchedPatterns;
    for (size_t i = 0; i < occurrences.size(); ++i) {
        if (i >= rowStates.size()) {
            break;
        }
        auto& state = rowStates[i];
        if (!state.hasPending()) {
            continue;
        }

        auto& occ = occurrences[i];
        bool success = true;
        std::string errorMessage;
        bool modifiedRow = false;

        auto captureUndo = [&]() {
            if (!touchedPatterns.count(occ.seqIndex)) {
                saveUndoState(occ.seqIndex);
                touchedPatterns.insert(occ.seqIndex);
            }
        };

        if (success && !state.newValue.empty()) {
            int newValue = 0;
            try {
                newValue = std::stoi(state.newValue);
            } catch (...) {
                success = false;
                errorMessage = "Invalid";
            }
            if (success) {
                if (!occ.valuePtr) {
                    success = false;
                    errorMessage = "Read-only";
                } else {
                    captureUndo();
                    applyVarChange(occ, newValue);
                    state.newValue.clear();
                    modifiedRow = true;
                }
            }
        }

        if (success && state.deltaPending) {
            if (supportsDeltaEdit(occ)) {
                captureUndo();
                applyDeltaValue(occ, state.deltaValue);
                state.deltaPending = false;
                modifiedRow = true;
            } else {
                success = false;
                errorMessage = "Delta unsupported";
            }
        }

        if (success && state.amountPending) {
            if (supportsAmountEdit(occ) && occ.valuePtr) {
                captureUndo();
                applyVarChange(occ, state.amountValue);
                state.amountPending = false;
                modifiedRow = true;
            } else {
                success = false;
                errorMessage = "Amount unsupported";
            }
        }

        if (success && state.compareValuePending) {
            if (occ.compareValuePtr) {
                captureUndo();
                *occ.compareValuePtr = state.compareValue;
                state.compareValuePending = false;
                modifiedRow = true;
                if (occ.sequence) {
                    occ.sequence->modified = true;
                }
            } else {
                success = false;
                errorMessage = "Compare unsupported";
            }
        }

        if (success && state.compareModePending) {
            if (occ.compareModePtr) {
                captureUndo();
                *occ.compareModePtr = state.compareMode;
                state.compareModePending = false;
                modifiedRow = true;
                if (occ.sequence) {
                    occ.sequence->modified = true;
                }
            } else {
                success = false;
                errorMessage = "Mode unsupported";
            }
        }

        if (success && state.changeValuePending) {
            if (occ.changeValuePtr) {
                captureUndo();
                *occ.changeValuePtr = state.changeValue;
                state.changeValuePending = false;
                modifiedRow = true;
                if (occ.sequence) {
                    occ.sequence->modified = true;
                }
            } else {
                success = false;
                errorMessage = "Value unsupported";
            }
        }

        if (success && state.changeModePending) {
            if (occ.changeModePtr) {
                captureUndo();
                *occ.changeModePtr = state.changeMode;
                state.changeModePending = false;
                modifiedRow = true;
                if (occ.sequence) {
                    occ.sequence->modified = true;
                }
            } else {
                success = false;
                errorMessage = "Mode unsupported";
            }
        }

        if (success && state.jumpTargetPending) {
            if (supportsJumpEdit(occ)) {
                captureUndo();
                applyJumpTarget(occ, state.jumpTargetValue, state.jumpTargetIsFrame || !occ.jumpTargetSupportsPattern);
                state.jumpTargetPending = false;
                modifiedRow = true;
            } else {
                success = false;
                errorMessage = "Jump unsupported";
            }
        }

        if (success && modifiedRow) {
            appliedAny = true;
        }

        if (success) {
            if (!modifiedRow) {
                state.clearPending();
                state.status = RowState::Status::None;
                state.statusMessage.clear();
            } else {
                state.clearPending();
                state.status = RowState::Status::Applied;
                state.statusMessage.clear();
            }
        } else {
            state.status = RowState::Status::Error;
            state.statusMessage = errorMessage;
        }
    }

    if (appliedAny) {
        markModified();
        invalidateSummaryCache();
    }
}

void VarSwapPane::drawParameterControls(int rowId, Occurrence& occ, RowState& state) {
    bool drewSomething = false;
    ImGui::PushID(rowId);
    ImGuiStyle& style = ImGui::GetStyle();
    auto inlineNext = [&](bool force = false) {
        if (drewSomething || force) {
            ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
        }
        drewSomething = true;
    };

    if (supportsDeltaEdit(occ)) {
        inlineNext();
        int deltaBase = deltaBaseFor(occ);
        int maxDelta = deltaBase - 1;
        int currentDelta = state.deltaPending ? state.deltaValue : currentDeltaValue(occ);
        currentDelta = std::clamp(currentDelta, 0, maxDelta);
        ImGui::SetNextItemWidth(60.f);
        int editDelta = currentDelta;
        if (ImGui::InputInt("##delta", &editDelta, 0, 0)) {
            editDelta = std::clamp(editDelta, 0, maxDelta);
            state.deltaPending = true;
            state.deltaValue = editDelta;
            state.status = RowState::Status::None;
            state.statusMessage.clear();
        }
        ImGui::SameLine(0.f, style.ItemInnerSpacing.x * 0.5f);
        const char* deltaLabel = "Δ";
        switch (occ.kind) {
            case OccurrenceKind::EfType6No100:
                deltaLabel = "Inc";
                break;
            case OccurrenceKind::EfType6No101:
            case OccurrenceKind::IfType2:
            case OccurrenceKind::IfType3:
            case OccurrenceKind::EfType1:
            case OccurrenceKind::EfType11:
                deltaLabel = "Dec";
                break;
            default:
                break;
        }
        ImGui::TextUnformatted(deltaLabel);
    }

    if (supportsAmountEdit(occ) && occ.valuePtr) {
        inlineNext();
        int currentAmount = state.amountPending ? state.amountValue : currentVar(occ);
        ImGui::SetNextItemWidth(80.f);
        int editAmount = currentAmount;
        if (ImGui::InputInt("##amount", &editAmount, 0, 0)) {
            state.amountPending = true;
            state.amountValue = editAmount;
            state.status = RowState::Status::None;
            state.statusMessage.clear();
        }
        ImGui::SameLine(0.f, style.ItemInnerSpacing.x * 0.5f);
        ImGui::TextUnformatted("Amount");
    }

    if (supportsComparisonEdit(occ)) {
        inlineNext();
        int compareValue = state.compareValuePending ? state.compareValue : (occ.compareValuePtr ? *occ.compareValuePtr : 0);
        ImGui::SetNextItemWidth(80.f);
        int editCompare = compareValue;
        if (ImGui::InputInt("##cmpValue", &editCompare, 0, 0)) {
            state.compareValuePending = true;
            state.compareValue = editCompare;
            state.status = RowState::Status::None;
            state.statusMessage.clear();
        }
        ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
        int currentMode = state.compareModePending ? state.compareMode : (occ.compareModePtr ? *occ.compareModePtr : 0);
        currentMode = std::clamp(currentMode, 0, 2);
        const char* preview = kCompareModeLabels[currentMode];
        const float comboWidth = ImGui::CalcTextSize("Greater (>)").x + style.FramePadding.x * 4.f + style.ItemInnerSpacing.x * 2.f + 10.f;
        ImGui::SetNextItemWidth(comboWidth);
        if (ImGui::BeginCombo("##cmpMode", preview)) {
            for (int mode = 0; mode < 3; ++mode) {
                bool selected = currentMode == mode;
                if (ImGui::Selectable(kCompareModeLabels[mode], selected)) {
                    state.compareModePending = true;
                    state.compareMode = mode;
                    state.status = RowState::Status::None;
                    state.statusMessage.clear();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    if (occ.changeValuePtr || occ.changeModePtr) {
        inlineNext();

        if (occ.changeValuePtr) {
            int currentValue = state.changeValuePending ? state.changeValue : *occ.changeValuePtr;
            ImGui::SetNextItemWidth(80.f);
            int editValue = currentValue;
            if (ImGui::InputInt("##chgValue", &editValue, 0, 0)) {
                state.changeValuePending = true;
                state.changeValue = editValue;
                state.status = RowState::Status::None;
                state.statusMessage.clear();
            }
        }

        if (occ.changeModePtr) {
            if (occ.changeValuePtr) {
                ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
            }

            int currentModeValue = state.changeModePending ? state.changeMode : *occ.changeModePtr;
            if (occ.kind == OccurrenceKind::IfType38) {
                const float comboWidth = [&]() {
                    float maxLabel = 0.f;
                    for (int idx = 0; idx < IM_ARRAYSIZE(kCond38ModeLabels); ++idx) {
                        maxLabel = std::max(maxLabel, ImGui::CalcTextSize(kCond38ModeLabels[idx]).x);
                    }
                    return maxLabel + style.FramePadding.x * 4.f + style.ItemInnerSpacing.x * 2.f + 10.f;
                }();
                int previewIdx = -1;
                for (int idx = 0; idx < IM_ARRAYSIZE(kCond38ModeValues); ++idx) {
                    if (currentModeValue == kCond38ModeValues[idx]) {
                        previewIdx = idx;
                        break;
                    }
                }
                char previewBuf[16];
                const char* preview = nullptr;
                if (previewIdx >= 0) {
                    preview = kCond38ModeLabels[previewIdx];
                } else {
                    std::snprintf(previewBuf, sizeof(previewBuf), "%d", currentModeValue);
                    preview = previewBuf;
                }
                ImGui::SetNextItemWidth(comboWidth);
                if (ImGui::BeginCombo("##chgMode", preview)) {
                    for (int idx = 0; idx < IM_ARRAYSIZE(kCond38ModeValues); ++idx) {
                        bool selected = (currentModeValue == kCond38ModeValues[idx]);
                        if (ImGui::Selectable(kCond38ModeLabels[idx], selected)) {
                            state.changeModePending = true;
                            state.changeMode = kCond38ModeValues[idx];
                            state.status = RowState::Status::None;
                            state.statusMessage.clear();
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            } else if (occ.kind == OccurrenceKind::EfType6No105) {
                int currentMode = std::clamp(currentModeValue, 0, 1);
                const float comboWidth = ImGui::CalcTextSize("Add").x + style.FramePadding.x * 4.f + style.ItemInnerSpacing.x * 2.f + 10.f;
                ImGui::SetNextItemWidth(comboWidth);
                if (ImGui::BeginCombo("##chgMode", kChangeModeLabels[currentMode])) {
                    for (int mode = 0; mode < 2; ++mode) {
                        bool selected = (currentMode == mode);
                        if (ImGui::Selectable(kChangeModeLabels[mode], selected)) {
                            state.changeModePending = true;
                            state.changeMode = mode;
                            state.status = RowState::Status::None;
                            state.statusMessage.clear();
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            } else {
                ImGui::SetNextItemWidth(80.f);
                int editMode = currentModeValue;
                if (ImGui::InputInt("##chgModeRaw", &editMode, 0, 0)) {
                    state.changeModePending = true;
                    state.changeMode = editMode;
                    state.status = RowState::Status::None;
                    state.statusMessage.clear();
                }
            }
        }
    }

    if (supportsJumpEdit(occ)) {
        inlineNext();
        int currentTarget = 0;
        bool currentIsFrame = true;
        decodeJumpTarget(occ, currentTarget, currentIsFrame);
        bool editIsFrame = state.jumpTargetPending ? state.jumpTargetIsFrame : currentIsFrame;
        int editValue = state.jumpTargetPending ? state.jumpTargetValue : currentTarget;

        const float controlHeight = ImGui::GetFrameHeight();
        if (occ.jumpTargetSupportsPattern) {
            bool toggle = editIsFrame;
            if (ImGui::Checkbox("Frame", &toggle)) {
                state.jumpTargetPending = true;
                state.jumpTargetIsFrame = toggle;
                state.jumpTargetValue = editValue;
                state.status = RowState::Status::None;
                state.statusMessage.clear();
                editIsFrame = toggle;
            }
            ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
        } else {
            ImGui::TextUnformatted("Frame jump");
            ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
        }

        if (!editIsFrame && occ.jumpTargetSupportsPattern) {
            int patternValue = editValue;
            std::string preview = sequenceDisplayName(patternValue);
            const char* popupId = "##patternPopup";
            ImGui::SetNextItemWidth(170.f);
            std::string buttonLabel = preview + "##patternPreview";
            if (ImGui::Button(buttonLabel.c_str())) {
                ImGui::OpenPopup(popupId);
            }
            if (ImGui::BeginPopup(popupId)) {
                if (frameData) {
                    int seqCount = frameData->get_sequence_count();
                    ImGui::SetNextItemWidth(120.f);
                    static char filterBuf[64] = {};
                    ImGui::InputText("##patternFilter", filterBuf, sizeof(filterBuf));
                    std::string filterLower = toLower(filterBuf);
                    ImGui::Separator();
                    ImGui::BeginChild("PatternList", ImVec2(240.f, 240.f), true);
                    for (int seqIdx = 0; seqIdx < seqCount; ++seqIdx) {
                        std::string label = sequenceDisplayName(seqIdx);
                        if (!filterLower.empty() && toLower(label).find(filterLower) == std::string::npos) {
                            continue;
                        }
                        bool selected = (patternValue == seqIdx);
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            patternValue = seqIdx;
                            state.jumpTargetPending = true;
                            state.jumpTargetIsFrame = false;
                            state.jumpTargetValue = patternValue;
                            state.status = RowState::Status::None;
                            state.statusMessage.clear();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::EndChild();
                }
                ImGui::EndPopup();
            }
        } else {
            ImGui::SetNextItemWidth(90.f);
            int numericValue = editValue;
            if (ImGui::InputInt("##jumpValue", &numericValue, 0, 0)) {
                numericValue = std::max(0, numericValue);
                state.jumpTargetPending = true;
                state.jumpTargetIsFrame = editIsFrame || !occ.jumpTargetSupportsPattern;
                state.jumpTargetValue = numericValue;
                state.status = RowState::Status::None;
                state.statusMessage.clear();
            }
        }
    }

    if (!drewSomething) {
        ImGui::Dummy(ImVec2(0.f, 0.f));
    }

    ImGui::PopID();
}

void VarSwapPane::applyGlobalReplace(const SummaryEntry& entry, int toVar) {
    if (!frameData || !frameData->m_loaded) {
        globalReplaceStatus = "Load a HA6 file first.";
        return;
    }

    if (!entry.hasNumericVarId) {
        globalReplaceStatus = "Global replace is not available for this category.";
        return;
    }

    if (entry.varId == toVar) {
        globalReplaceStatus = "Choose a different target value.";
        return;
    }

    if (rowStates.size() != occurrences.size()) {
        rowStates.resize(occurrences.size());
    }

    bool requireProjectile = (entry.category == VarCategory::Projectile);
    bool requireGlobalProjectile = requireProjectile && entry.isProjectileGlobal;
    bool requireLocalProjectile = requireProjectile && !entry.isProjectileGlobal;

    int queued = 0;
    size_t count = std::min(occurrences.size(), rowStates.size());
    for (size_t i = 0; i < count; ++i) {
        auto& occ = occurrences[i];
        if (occ.category != entry.category) {
            continue;
        }
        if (currentVar(occ) != entry.varId || !occ.valuePtr) {
            continue;
        }
        const auto& meta = metaFor(i);
        if (requireGlobalProjectile && !meta.isGlobalProjectile) {
            continue;
        }
        if (requireLocalProjectile && meta.isGlobalProjectile) {
            continue;
        }
        auto& state = rowStates[i];
        state.newValue = std::to_string(toVar);
        state.status = RowState::Status::None;
        state.statusMessage.clear();
        queued++;
    }

    if (queued == 0) {
        globalReplaceStatus = "No matching occurrences available to queue.";
        return;
    }

    globalReplaceInput.clear();
    invalidateSummaryCache();
    globalReplaceStatus = std::to_string(queued) + " occurrence(s) queued. Apply pending to commit.";
}

void VarSwapPane::markModified() {
    if (onModified) {
        onModified();
    }
}

void VarSwapPane::saveUndoState(int patternIndex) {
    if (onSaveUndo) {
        onSaveUndo(patternIndex);
    }
}

void VarSwapPane::clearPendingEdits() {
    for (auto& state : rowStates) {
        state.clearPending();
    }
}

bool VarSwapPane::hasPendingEdits() const {
    for (const auto& state : rowStates) {
        if (state.hasPending()) {
            return true;
        }
    }
    return false;
}

bool VarSwapPane::supportsDeltaEdit(const Occurrence& occ) const {
    if (!occ.valuePtr) {
        return false;
    }
    switch (occ.kind) {
        case OccurrenceKind::EfType6No100:
        case OccurrenceKind::EfType6No101:
        case OccurrenceKind::IfType2:
        case OccurrenceKind::IfType3:
        case OccurrenceKind::EfType1:
        case OccurrenceKind::EfType11:
            return true;
        default:
            return false;
    }
}

bool VarSwapPane::supportsAmountEdit(const Occurrence& occ) const {
    return occ.kind == OccurrenceKind::EfType6No102 ||
           occ.kind == OccurrenceKind::EfType6No103;
}

int VarSwapPane::currentDeltaValue(const Occurrence& occ) const {
    if (!occ.valuePtr) {
        return 0;
    }
    int maxDelta = deltaBaseFor(occ) - 1;
    return std::clamp(compositeRemainder(occ), 0, maxDelta);
}

void VarSwapPane::applyDeltaValue(Occurrence& occ, int newDelta) const {
    if (!occ.valuePtr) {
        return;
    }
    int base = deltaBaseFor(occ);
    auto parts = std::div(*occ.valuePtr, base);
    newDelta = std::clamp(newDelta, 0, base - 1);
    *occ.valuePtr = parts.quot * base + newDelta;
    if (occ.sequence) {
        occ.sequence->modified = true;
    }
}

bool VarSwapPane::supportsComparisonEdit(const Occurrence& occ) const {
    return occ.compareValuePtr && occ.compareModePtr;
}

bool VarSwapPane::supportsJumpEdit(const Occurrence& occ) const {
    return occ.jumpTargetPtr != nullptr;
}

int VarSwapPane::deltaBaseFor(const Occurrence& occ) const {
    if (occ.encoding == ValueEncoding::HundredsComposite) {
        return 100;
    }
    if (occ.encoding == ValueEncoding::ProjectileComposite && occ.valuePtr) {
        return (std::abs(*occ.valuePtr) >= 100) ? 100 : 10;
    }
    return 10;
}

bool VarSwapPane::isProjectileGlobalOp(const Occurrence& occ) const {
    if (!occ.valuePtr) {
        return false;
    }
    if (occ.kind != OccurrenceKind::EfType6No100 && occ.kind != OccurrenceKind::EfType6No101) {
        return false;
    }
    if (occ.encoding != ValueEncoding::ProjectileComposite) {
        return false;
    }
    return std::abs(*occ.valuePtr) >= 100;
}

void VarSwapPane::decodeJumpTarget(const Occurrence& occ, int& valueOut, bool& isFrameOut) const {
    valueOut = 0;
    isFrameOut = true;
    if (!occ.jumpTargetPtr) {
        return;
    }
    int raw = *occ.jumpTargetPtr;
    if (occ.jumpTargetSupportsPattern && raw >= kFrameJumpOffset) {
        valueOut = std::max(0, raw - kFrameJumpOffset);
        isFrameOut = false;
    } else {
        valueOut = std::max(0, raw);
        isFrameOut = true;
    }
}

void VarSwapPane::applyJumpTarget(Occurrence& occ, int target, bool asFrame) const {
    if (!occ.jumpTargetPtr) {
        return;
    }
    target = std::max(0, target);
    if (occ.jumpTargetSupportsPattern && !asFrame) {
        *occ.jumpTargetPtr = target + kFrameJumpOffset;
    } else {
        *occ.jumpTargetPtr = target;
    }
    if (occ.sequence) {
        occ.sequence->modified = true;
    }
}

std::string VarSwapPane::sequenceDisplayName(int seqIndex) const {
    if (!frameData || seqIndex < 0 || seqIndex >= frameData->get_sequence_count()) {
        return std::to_string(seqIndex);
    }
    Sequence* seq = frameData->get_sequence(seqIndex);
    if (!seq) {
        return std::to_string(seqIndex);
    }
    return formatSequenceLabel(*seq, seqIndex);
}

bool VarSwapPane::drawPatternCombo(const char* label, int* value) const {
    if (!frameData || !value) {
        return false;
    }
    int seqCount = frameData->get_sequence_count();
    if (seqCount <= 0) {
        return false;
    }
    *value = std::clamp(*value, 0, seqCount - 1);
    std::string preview = sequenceDisplayName(*value);
    bool changed = false;
    if (ImGui::BeginCombo(label, preview.c_str())) {
        for (int i = 0; i < seqCount; ++i) {
            std::string option = sequenceDisplayName(i);
            bool selected = (*value == i);
            if (ImGui::Selectable(option.c_str(), selected)) {
                *value = i;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

std::string VarSwapPane::buildPatternLabel(const Occurrence& occ) {
    if (occ.sequence) {
        return formatSequenceLabel(*occ.sequence, occ.seqIndex);
    }
    return std::to_string(occ.seqIndex);
}

std::string VarSwapPane::buildNodeLabel(const Occurrence& occ) {
    std::ostringstream oss;
    oss << kindLabel(occ.kind);
    if (occ.ifBlock) {
        oss << " [IF #" << occ.blockIndex << "]";
    } else if (occ.efBlock) {
        oss << " [EF #" << occ.blockIndex;
        if (occ.efBlock) {
            oss << ", no " << occ.efBlock->number;
        }
        oss << "]";
    }
    return oss.str();
}

bool VarSwapPane::isProjectileGlobal(int varId) const {
    return varId >= 60000;
}

std::string VarSwapPane::describeProjectileVar(int varId) const {
    if (!isProjectileGlobal(varId)) {
        return "Frame-local projectile variable";
    }

    if (varId >= 60000 && varId < 60100) {
        return "Global projectile register bank A";
    }
    if (varId >= 60100 && varId < 60200) {
        return "Global projectile register bank B";
    }
    if (varId >= 61440 && varId < 61600) {
        return "Extended global projectile register";
    }
    return "Global projectile register (shared across all projectiles)";
}
