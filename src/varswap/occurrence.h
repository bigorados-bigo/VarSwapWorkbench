#ifndef VARSWAP_OCCURRENCE_H_GUARD
#define VARSWAP_OCCURRENCE_H_GUARD

#include "framedata.h"

#include <string>
#include <vector>

namespace varswap {

enum class VarCategory {
    Projectile,
    ProjectileNoChange,
    Extra,
    Dash,
    Assist,
    Unknown,
    Count
};

enum class OccurrenceKind {
    IfType2,
    IfType3,
    IfType24,
    IfType25,
    IfType31,
    IfType38,
    EfType1,
    EfType11,
    EfType6No100,
    EfType6No101,
    EfType6No102,
    EfType6No103,
    EfType6No105
};

enum class ValueEncoding {
    Direct,
    TensComposite,
    HundredsComposite,
    ProjectileComposite
};

struct Occurrence {
    OccurrenceKind kind;
    VarCategory category;
    Sequence* sequence;
    Frame* frame;
    Frame_IF* ifBlock;
    Frame_EF* efBlock;
    int seqIndex;
    int frameIndex;
    int blockIndex;
    ValueEncoding encoding;
    int* valuePtr;
    int* compareValuePtr = nullptr;
    int* compareModePtr = nullptr;
    int* jumpTargetPtr = nullptr;
    bool jumpTargetSupportsPattern = false;
    int* changeValuePtr = nullptr;
    int* changeModePtr = nullptr;
};

std::vector<Occurrence> collectOccurrences(FrameData& data);
std::string kindLabel(OccurrenceKind kind);
std::string kindCode(OccurrenceKind kind);
int currentVar(const Occurrence& occ);
int compositeRemainder(const Occurrence& occ);
void applyVarChange(Occurrence& occ, int newVar);
const char* categoryLabel(VarCategory category);

} // namespace varswap

#endif /* VARSWAP_OCCURRENCE_H_GUARD */
