#include "varswap/occurrence.h"

#include <algorithm>
#include <cstdlib>

namespace varswap {

namespace {

bool isProjectileGlobalRaw(int raw) {
    return raw >= 100 || raw <= -100;
}

int projectileBase(int raw) {
    return isProjectileGlobalRaw(raw) ? 100 : 10;
}

int projectileVarFromRaw(int raw) {
    int base = projectileBase(raw);
    auto parts = std::div(raw, base);
    return parts.quot;
}

int projectileDeltaFromRaw(int raw) {
    int base = projectileBase(raw);
    int remainder = raw % base;
    if (remainder < 0) {
        remainder += base;
    }
    return remainder;
}

int composeProjectileRaw(int varId, int delta, int base) {
    delta = std::clamp(delta, 0, base - 1);
    return varId * base + delta;
}

void applyProjectileCategoryOverrides(Occurrence& occ) {
    if (occ.category != VarCategory::Projectile) {
        return;
    }
    if (!occ.valuePtr) {
        return;
    }
    if (occ.encoding != ValueEncoding::TensComposite && occ.encoding != ValueEncoding::ProjectileComposite) {
        return;
    }
    int raw = *occ.valuePtr;
    int base = (occ.encoding == ValueEncoding::ProjectileComposite) ? projectileBase(raw) : 10;
    if (base <= 0) {
        return;
    }
    int remainder = raw % base;
    if (remainder < 0) {
        remainder += base;
    }
    if (remainder == 0) {
        occ.category = VarCategory::ProjectileNoChange;
    }
}

Occurrence makeOccurrence(OccurrenceKind kind,
                          VarCategory category,
                          Sequence* seq,
                          Frame* frame,
                          Frame_IF* ifBlock,
                          Frame_EF* efBlock,
                          int seqIndex,
                          int frameIndex,
                          int blockIndex,
                          ValueEncoding encoding,
                          int* valuePtr) {
    Occurrence occ{kind, category, seq, frame, ifBlock, efBlock, seqIndex, frameIndex, blockIndex, encoding, valuePtr};
    applyProjectileCategoryOverrides(occ);
    return occ;
}

} // namespace

std::vector<Occurrence> collectOccurrences(FrameData& data) {
    std::vector<Occurrence> result;
    int seqCount = data.get_sequence_count();
    for (int seqIdx = 0; seqIdx < seqCount; ++seqIdx) {
        Sequence* seq = data.get_sequence(seqIdx);
        if (!seq) {
            continue;
        }
        for (size_t frameIdx = 0; frameIdx < seq->frames.size(); ++frameIdx) {
            Frame& frame = seq->frames[frameIdx];

            for (size_t ifIdx = 0; ifIdx < frame.IF.size(); ++ifIdx) {
                Frame_IF& cond = frame.IF[ifIdx];
                switch (cond.type) {
                    case 2:
                        result.emplace_back(makeOccurrence(OccurrenceKind::IfType2, VarCategory::Projectile,
                                                           seq, &frame, &cond, nullptr, seqIdx,
                                                           static_cast<int>(frameIdx), static_cast<int>(ifIdx),
                                                           ValueEncoding::TensComposite, &cond.parameters[3]));
                        break;
                    case 3:
                        {
                            auto& occ = result.emplace_back(makeOccurrence(OccurrenceKind::IfType3, VarCategory::Projectile,
                                                            seq, &frame, &cond, nullptr, seqIdx,
                                                            static_cast<int>(frameIdx), static_cast<int>(ifIdx),
                                                            ValueEncoding::TensComposite, &cond.parameters[3]));
                            occ.jumpTargetPtr = &cond.parameters[0];
                            occ.jumpTargetSupportsPattern = true;
                        }
                        break;
                    case 24:
                        {
                            auto& occ = result.emplace_back(makeOccurrence(OccurrenceKind::IfType24, VarCategory::Projectile,
                                                            seq, &frame, &cond, nullptr, seqIdx,
                                                            static_cast<int>(frameIdx), static_cast<int>(ifIdx),
                                                            ValueEncoding::TensComposite, &cond.parameters[1]));
                            occ.jumpTargetPtr = &cond.parameters[0];
                            occ.jumpTargetSupportsPattern = true;
                        }
                        break;
                    case 25:
                        {
                            auto& occ = result.emplace_back(makeOccurrence(OccurrenceKind::IfType25, VarCategory::Extra,
                                                            seq, &frame, &cond, nullptr, seqIdx,
                                                            static_cast<int>(frameIdx), static_cast<int>(ifIdx),
                                                            ValueEncoding::Direct, &cond.parameters[1]));
                            occ.compareValuePtr = &cond.parameters[2];
                            occ.compareModePtr = &cond.parameters[3];
                            occ.jumpTargetPtr = &cond.parameters[0];
                            occ.jumpTargetSupportsPattern = true;
                        }
                        break;
                    case 31:
                        {
                            auto& occ = result.emplace_back(makeOccurrence(OccurrenceKind::IfType31, VarCategory::Extra,
                                                            seq, &frame, &cond, nullptr, seqIdx,
                                                            static_cast<int>(frameIdx), static_cast<int>(ifIdx),
                                                            ValueEncoding::Direct, &cond.parameters[1]));
                            occ.changeValuePtr = &cond.parameters[2];
                        }
                        break;
                    case 38:
                        {
                            auto& occ = result.emplace_back(makeOccurrence(OccurrenceKind::IfType38, VarCategory::Extra,
                                                            seq, &frame, &cond, nullptr, seqIdx,
                                                            static_cast<int>(frameIdx), static_cast<int>(ifIdx),
                                                            ValueEncoding::Direct, &cond.parameters[3]));
                            occ.changeValuePtr = &cond.parameters[0];
                            occ.changeModePtr = &cond.parameters[4];
                        }
                        break;
                    default:
                        break;
                }
            }

            for (size_t efIdx = 0; efIdx < frame.EF.size(); ++efIdx) {
                Frame_EF& effect = frame.EF[efIdx];
                switch (effect.type) {
                    case 1:
                    case 101:
                        result.emplace_back(makeOccurrence(OccurrenceKind::EfType1, VarCategory::Projectile,
                                                          seq, &frame, nullptr, &effect, seqIdx,
                                                          static_cast<int>(frameIdx), static_cast<int>(efIdx),
                                                          ValueEncoding::TensComposite, &effect.parameters[8]));
                        break;
                    case 11:
                    case 111:
                        result.emplace_back(makeOccurrence(OccurrenceKind::EfType11, VarCategory::Projectile,
                                                          seq, &frame, nullptr, &effect, seqIdx,
                                                          static_cast<int>(frameIdx), static_cast<int>(efIdx),
                                                          ValueEncoding::TensComposite, &effect.parameters[9]));
                        break;
                    case 6:
                        switch (effect.number) {
                            case 100:
                                result.push_back(makeOccurrence(OccurrenceKind::EfType6No100, VarCategory::Projectile,
                                                            seq, &frame, nullptr, &effect, seqIdx,
                                                            static_cast<int>(frameIdx), static_cast<int>(efIdx),
                                                            ValueEncoding::ProjectileComposite, &effect.parameters[0]));
                                break;
                            case 101:
                                result.push_back(makeOccurrence(OccurrenceKind::EfType6No101, VarCategory::Projectile,
                                                            seq, &frame, nullptr, &effect, seqIdx,
                                                            static_cast<int>(frameIdx), static_cast<int>(efIdx),
                                                            ValueEncoding::ProjectileComposite, &effect.parameters[0]));
                                break;
                            case 102:
                                result.push_back(makeOccurrence(OccurrenceKind::EfType6No102, VarCategory::Dash,
                                                            seq, &frame, nullptr, &effect, seqIdx,
                                                            static_cast<int>(frameIdx), static_cast<int>(efIdx),
                                                            ValueEncoding::Direct, &effect.parameters[0]));
                                break;
                            case 103:
                                result.push_back(makeOccurrence(OccurrenceKind::EfType6No103, VarCategory::Dash,
                                                            seq, &frame, nullptr, &effect, seqIdx,
                                                            static_cast<int>(frameIdx), static_cast<int>(efIdx),
                                                            ValueEncoding::Direct, &effect.parameters[0]));
                                break;
                            case 105:
                                {
                                    auto& occ = result.emplace_back(makeOccurrence(OccurrenceKind::EfType6No105, VarCategory::Extra,
                                                                seq, &frame, nullptr, &effect, seqIdx,
                                                                static_cast<int>(frameIdx), static_cast<int>(efIdx),
                                                                ValueEncoding::Direct, &effect.parameters[0]));
                                    occ.changeValuePtr = &effect.parameters[1];
                                    occ.changeModePtr = &effect.parameters[2];
                                }
                                break;
                            default:
                                break;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }
    return result;
}

std::string kindLabel(OccurrenceKind kind) {
    switch (kind) {
        case OccurrenceKind::IfType2:
            return "IF type 2 (Effect despawn var delta)";
        case OccurrenceKind::IfType3:
            return "IF type 3 (Branch on hit var delta)";
        case OccurrenceKind::IfType24:
            return "IF type 24 (Projectile variable check)";
        case OccurrenceKind::IfType25:
            return "IF type 25 (Variable comparison)";
        case OccurrenceKind::IfType31:
            return "IF type 31 (Change variable on command)";
        case OccurrenceKind::IfType38:
            return "IF type 38 (Change variable on hit)";
        case OccurrenceKind::EfType1:
            return "EF type 1/101 (Spawn pattern var delta)";
        case OccurrenceKind::EfType11:
            return "EF type 11/111 (Random spawn var delta)";
        case OccurrenceKind::EfType6No100:
            return "EF type 6 #100 (Increase projectile variable)";
        case OccurrenceKind::EfType6No101:
            return "EF type 6 #101 (Decrease projectile variable)";
        case OccurrenceKind::EfType6No102:
            return "EF type 6 #102 (Increase dash variable)";
        case OccurrenceKind::EfType6No103:
            return "EF type 6 #103 (Decrease dash variable)";
        case OccurrenceKind::EfType6No105:
            return "EF type 6 #105 (Change variable)";
    }
    return "Unknown";
}

std::string kindCode(OccurrenceKind kind) {
    switch (kind) {
        case OccurrenceKind::IfType2:
            return "IF02";
        case OccurrenceKind::IfType3:
            return "IF03";
        case OccurrenceKind::IfType24:
            return "IF24";
        case OccurrenceKind::IfType25:
            return "IF25";
        case OccurrenceKind::IfType31:
            return "IF31";
        case OccurrenceKind::IfType38:
            return "IF38";
        case OccurrenceKind::EfType1:
            return "EF1";
        case OccurrenceKind::EfType11:
            return "EF11";
        case OccurrenceKind::EfType6No100:
            return "EF6-100";
        case OccurrenceKind::EfType6No101:
            return "EF6-101";
        case OccurrenceKind::EfType6No102:
            return "EF6-102";
        case OccurrenceKind::EfType6No103:
            return "EF6-103";
        case OccurrenceKind::EfType6No105:
            return "EF6-105";
    }
    return "?";
}

int currentVar(const Occurrence& occ) {
    if (!occ.valuePtr) {
        return 0;
    }
    int raw = *occ.valuePtr;
    if (occ.encoding == ValueEncoding::Direct) {
        return raw;
    }
    if (occ.encoding == ValueEncoding::ProjectileComposite) {
        return projectileVarFromRaw(raw);
    }
    if (occ.encoding == ValueEncoding::TensComposite) {
        auto parts = std::div(raw, 10);
        return parts.quot;
    }
    auto parts = std::div(raw, 100);
    return parts.quot;
}

int compositeRemainder(const Occurrence& occ) {
    if (!occ.valuePtr) {
        return 0;
    }
    if (occ.encoding == ValueEncoding::ProjectileComposite) {
        return projectileDeltaFromRaw(*occ.valuePtr);
    }
    if (occ.encoding == ValueEncoding::TensComposite) {
        auto parts = std::div(*occ.valuePtr, 10);
        return parts.rem;
    }
    if (occ.encoding == ValueEncoding::HundredsComposite) {
        auto parts = std::div(*occ.valuePtr, 100);
        return parts.rem;
    }
    return 0;
}

void applyVarChange(Occurrence& occ, int newVar) {
    if (!occ.valuePtr) {
        return;
    }
    if (occ.encoding == ValueEncoding::Direct) {
        *occ.valuePtr = newVar;
    } else if (occ.encoding == ValueEncoding::ProjectileComposite) {
        int raw = *occ.valuePtr;
        int base = projectileBase(raw);
        int delta = projectileDeltaFromRaw(raw);
        *occ.valuePtr = composeProjectileRaw(newVar, delta, base);
    } else if (occ.encoding == ValueEncoding::TensComposite) {
        auto parts = std::div(*occ.valuePtr, 10);
        *occ.valuePtr = newVar * 10 + parts.rem;
    } else {
        auto parts = std::div(*occ.valuePtr, 100);
        *occ.valuePtr = newVar * 100 + parts.rem;
    }
    if (occ.sequence) {
        occ.sequence->modified = true;
    }
}

const char* categoryLabel(VarCategory category) {
    switch (category) {
        case VarCategory::Projectile:
            return "Projectile";
        case VarCategory::ProjectileNoChange:
            return "Proj. Null";
        case VarCategory::Extra:
            return "Extra";
        case VarCategory::Dash:
            return "Dash";
        case VarCategory::Assist:
            return "Assist";
        case VarCategory::Unknown:
        default:
            return "Unknown";
    }
}

} // namespace varswap
