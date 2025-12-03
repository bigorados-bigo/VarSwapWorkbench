#ifndef VARSWAP_PANE_H_GUARD
#define VARSWAP_PANE_H_GUARD

#include "framedata.h"
#include "varswap/occurrence.h"

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

class VarSwapPane {
public:
    explicit VarSwapPane(FrameData* frameData);
    void Draw();
    void ForceRescan();
    bool hasPendingEdits() const;

    bool isVisible = true;
    std::function<void()> onModified;
    std::function<void(int)> onSaveUndo;

private:
    struct RowState {
        bool selected = false;
        std::string newValue;
        enum class Status { None, Applied, Error } status = Status::None;
        std::string statusMessage;
        bool deltaPending = false;
        int deltaValue = 0;
        bool amountPending = false;
        int amountValue = 0;
        bool compareValuePending = false;
        int compareValue = 0;
        bool compareModePending = false;
        int compareMode = 0;
        bool changeValuePending = false;
        int changeValue = 0;
        bool changeModePending = false;
        int changeMode = 0;
        bool jumpTargetPending = false;
        int jumpTargetValue = 0;
        bool jumpTargetIsFrame = false;
        bool hasPending() const {
            return !newValue.empty() || deltaPending || compareValuePending || compareModePending ||
                   amountPending || changeValuePending || changeModePending || jumpTargetPending;
        }
        void clearPending() {
            newValue.clear();
            deltaPending = false;
            amountPending = false;
            compareValuePending = false;
            compareModePending = false;
            changeValuePending = false;
            changeModePending = false;
            jumpTargetPending = false;
            status = Status::None;
            statusMessage.clear();
        }
    };

    struct SummaryEntry {
        int varId = 0;
        int count = 0;
        varswap::VarCategory category = varswap::VarCategory::Unknown;
        bool hasNumericVarId = true;
        std::unordered_map<int, int> patternCounts;
        std::vector<std::pair<int, int>> sortedPatterns;
        bool patternsDirty = true;
        std::string label;
        int globalOpCount = 0;
        int globalIncreaseTotal = 0;
        int globalDecreaseTotal = 0;
        bool isProjectileGlobal = false;
        std::uint64_t cacheKey = 0;
    };

    struct OccurrenceMetadata {
        std::string patternLabel;
        std::string nodeLabel;
        std::string searchLower;
        bool isGlobalProjectile = false;
        int globalVar = 0;
        int globalDelta = 0;
        bool globalDecrement = false;
    };

    FrameData* frameData;
    std::vector<varswap::Occurrence> occurrences;
    std::vector<OccurrenceMetadata> occurrenceMetadata;
    std::vector<RowState> rowStates;
    bool categoryVisibility[static_cast<int>(varswap::VarCategory::Count)];
    std::string searchText;
    bool pendingOnly = false;
    bool projectileGlobalsOnly = false;
    std::uint64_t selectedSummaryKey = 0;
    int selectedVarId = std::numeric_limits<int>::min();
    varswap::VarCategory selectedCategory = varswap::VarCategory::Count;
    bool selectedProjectileFlagValid = false;
    bool selectedProjectileIsGlobal = false;
    char infoLabel[64] = {0};
    std::string globalReplaceInput;
    std::string globalReplaceStatus;
    int lastGlobalSelection = std::numeric_limits<int>::min();
    varswap::VarCategory lastGlobalSelectionCategory = varswap::VarCategory::Count;
    std::vector<SummaryEntry> summaryCache;
    std::vector<int> summaryCacheVisibleIndices;
    bool summaryCacheDirty = true;
    bool showPendingListWindow = false;

    void refreshScan();
    void ensureRowStateSize();
    void drawEmptyState();
    void drawSummary(const std::vector<int>& visibleIndices);
    void drawVarDetailPanel(SummaryEntry* entry);
    void drawPatternList(SummaryEntry* entry);
    void drawGlobalReplaceControls(const SummaryEntry* entry);
    void drawTable(const std::vector<int>& visibleIndices);
    void drawPendingListWindow();
    void drawParameterControls(int rowId, varswap::Occurrence& occ, RowState& state);
    void sortSummaryEntries(std::vector<int>& order, const std::vector<SummaryEntry>& entries, const ImGuiTableSortSpecs* sortSpecs);
    void sortOccurrenceIndices(std::vector<int>& indices, const ImGuiTableSortSpecs* sortSpecs);
    std::vector<SummaryEntry> buildSummaryEntries(const std::vector<int>& visibleIndices);
    std::vector<SummaryEntry>& getSummaryEntries(const std::vector<int>& visibleIndices);
    void invalidateSummaryCache();
    SummaryEntry* findSummaryEntryByKey(std::uint64_t key);
    const std::vector<std::pair<int, int>>& ensureSortedPatterns(SummaryEntry& entry);
    std::vector<int> buildVisibleIndexList();
    bool matchesFilters(size_t index, const std::string& needleLower);
    void rebuildOccurrenceMetadata();
    const OccurrenceMetadata& metaFor(size_t index) const;
    void applyPendingChanges();
    void applyGlobalReplace(const SummaryEntry& entry, int toVar);
    void clearPendingEdits();
    static std::string buildPatternLabel(const varswap::Occurrence& occ);
    static std::string buildNodeLabel(const varswap::Occurrence& occ);
    std::string describePendingAction(size_t index) const;
    bool isProjectileGlobal(int varId) const;
    std::string describeProjectileVar(int varId) const;
    bool supportsDeltaEdit(const varswap::Occurrence& occ) const;
    int currentDeltaValue(const varswap::Occurrence& occ) const;
    void applyDeltaValue(varswap::Occurrence& occ, int newDelta) const;
    bool supportsComparisonEdit(const varswap::Occurrence& occ) const;
    bool supportsJumpEdit(const varswap::Occurrence& occ) const;
    void applyJumpTarget(varswap::Occurrence& occ, int target, bool asFrame) const;
    void decodeJumpTarget(const varswap::Occurrence& occ, int& valueOut, bool& isFrameOut) const;
    std::string sequenceDisplayName(int seqIndex) const;
    bool drawPatternCombo(const char* label, int* value) const;

    void markModified();
    void saveUndoState(int patternIndex);
        int deltaBaseFor(const varswap::Occurrence& occ) const;
        bool isProjectileGlobalOp(const varswap::Occurrence& occ) const;
    bool supportsAmountEdit(const varswap::Occurrence& occ) const;
};

#endif /* VARSWAP_PANE_H_GUARD */
