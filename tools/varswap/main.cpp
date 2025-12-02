#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "framedata.h"
#include "varswap/occurrence.h"

namespace fs = std::filesystem;

using varswap::Occurrence;
using varswap::OccurrenceKind;
using varswap::VarCategory;
using varswap::ValueEncoding;
using varswap::applyVarChange;
using varswap::collectOccurrences;
using varswap::compositeRemainder;
using varswap::currentVar;
using varswap::kindCode;
using varswap::kindLabel;
using varswap::categoryLabel;

struct LogEntry {
    int fromVar;
    int toVar;
    int patternIndex;
    std::string patternLabel;
    int frameIndex;
    std::string nodeLabel;
    int rawBefore;
    int rawAfter;
};

bool parseInt(const std::string& text, int& out) {
    try {
        size_t idx = 0;
        int value = std::stoi(text, &idx, 10);
        if (idx != text.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

std::string sequenceLabel(const Sequence& seq, int seqIndex) {
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

std::string nodeLabel(const Occurrence& occ) {
    std::ostringstream oss;
    oss << kindLabel(occ.kind);
    if (occ.ifBlock) {
        oss << " [IF #" << occ.blockIndex << "]";
    } else if (occ.efBlock) {
        oss << " [EF #" << occ.blockIndex << ", no " << occ.efBlock->number << "]";
    }
    return oss.str();
}

void describeOccurrence(const Occurrence& occ, std::ostream& os) {
    const Sequence& seq = *occ.sequence;
    os << "- Pattern " << sequenceLabel(seq, occ.seqIndex)
       << ", Frame " << occ.frameIndex
       << ", " << kindLabel(occ.kind)
       << " [" << categoryLabel(occ.category) << "]";

    if (occ.ifBlock) {
        os << " [IF #" << occ.blockIndex << "]";
    } else if (occ.efBlock) {
        os << " [EF #" << occ.blockIndex;
        os << ", no " << occ.efBlock->number << "]";
    }

    os << ", var " << currentVar(occ)
       << ", raw " << (occ.valuePtr ? *occ.valuePtr : 0);

    if (occ.encoding == ValueEncoding::TensComposite && occ.valuePtr) {
        os << " (value digit " << compositeRemainder(occ) << ")";
    }

    os << '\n';
}


void printUsage() {
    std::cout << "Usage:\n"
              << "  ha6_var_tool scan --file <path> [--var <id>]\n"
              << "  ha6_var_tool replace --file <path> --from <id> --to <id> [--out <path> | --in-place] [--dry-run] [--log <path>] [--no-log]\n";
}

fs::path defaultOutputPath(const fs::path& input) {
    fs::path parent = input.parent_path();
    std::string stem = input.stem().string();
    std::string ext = input.extension().string();
    fs::path name = stem + ".varswap" + ext;
    return parent / name;
}

fs::path defaultLogPath(const fs::path& targetFile) {
    fs::path parent = targetFile.parent_path();
    std::string stem = targetFile.stem().string();
    fs::path name = stem + "_varswap_log.csv";
    return parent / name;
}

std::string csvEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

void writeLogFile(const fs::path& logPath, const fs::path& targetFile,
                  const std::vector<LogEntry>& entries) {
    if (entries.empty()) {
        return;
    }

    if (!logPath.parent_path().empty()) {
        std::error_code ec;
        fs::create_directories(logPath.parent_path(), ec);
    }

    bool needsHeader = !fs::exists(logPath);
    std::ofstream out(logPath, std::ios::app);
    if (!out) {
        std::cerr << "Warning: failed to open log file " << logPath
                  << " for writing." << std::endl;
        return;
    }

    if (needsHeader) {
        out << "\"File\",\"FromVar\",\"ToVar\",\"PatternIndex\",\"PatternLabel\",\"Frame\",\"Node\",\"RawBefore\",\"RawAfter\"\n";
    }

    std::string fileLabel = targetFile.filename().string();
    for (const auto& entry : entries) {
        out << csvEscape(fileLabel) << ','
            << entry.fromVar << ','
            << entry.toVar << ','
            << entry.patternIndex << ','
            << csvEscape(entry.patternLabel) << ','
            << entry.frameIndex << ','
            << csvEscape(entry.nodeLabel) << ','
            << entry.rawBefore << ','
            << entry.rawAfter << '\n';
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printUsage();
        return 1;
    }

    std::string command = argv[1];
    fs::path inputPath;
    std::optional<int> scanVar;
    std::optional<int> fromVar;
    std::optional<int> toVar;
    std::optional<fs::path> outPath;
    std::optional<fs::path> logPath;
    bool inPlace = false;
    bool dryRun = false;
    bool disableLog = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--file" && i + 1 < argc) {
            inputPath = argv[++i];
        } else if (command == "scan" && arg == "--var" && i + 1 < argc) {
            int parsed = 0;
            if (!parseInt(argv[++i], parsed)) {
                std::cerr << "Invalid value for --var" << std::endl;
                return 1;
            }
            scanVar = parsed;
        } else if (command == "replace" && arg == "--from" && i + 1 < argc) {
            int parsed = 0;
            if (!parseInt(argv[++i], parsed)) {
                std::cerr << "Invalid value for --from" << std::endl;
                return 1;
            }
            fromVar = parsed;
        } else if (command == "replace" && arg == "--to" && i + 1 < argc) {
            int parsed = 0;
            if (!parseInt(argv[++i], parsed)) {
                std::cerr << "Invalid value for --to" << std::endl;
                return 1;
            }
            toVar = parsed;
        } else if (command == "replace" && arg == "--out" && i + 1 < argc) {
            outPath = argv[++i];
        } else if (command == "replace" && arg == "--in-place") {
            inPlace = true;
        } else if (command == "replace" && arg == "--dry-run") {
            dryRun = true;
        } else if (command == "replace" && arg == "--log" && i + 1 < argc) {
            logPath = argv[++i];
        } else if (command == "replace" && arg == "--no-log") {
            disableLog = true;
        } else {
            std::cerr << "Unknown or misplaced argument: " << arg << std::endl;
            printUsage();
            return 1;
        }
    }

    if (inputPath.empty()) {
        std::cerr << "--file is required" << std::endl;
        return 1;
    }

    FrameData data;
    if (!data.load(inputPath.string().c_str())) {
        std::cerr << "Failed to load HA6 file: " << inputPath << std::endl;
        return 1;
    }

    auto occurrences = collectOccurrences(data);

    if (command == "scan") {
        int totalMatches = 0;
        std::map<OccurrenceKind, int> perKind;
        for (auto& occ : occurrences) {
            if (scanVar && currentVar(occ) != *scanVar) {
                continue;
            }
            describeOccurrence(occ, std::cout);
            ++totalMatches;
            perKind[occ.kind]++;
        }

        if (totalMatches == 0) {
            if (scanVar) {
                std::cout << "No occurrences with var " << *scanVar << " found." << std::endl;
            } else {
                std::cout << "No variable references found." << std::endl;
            }
        } else {
            std::cout << "\n" << totalMatches << " occurrence(s) listed." << std::endl;
            if (!perKind.empty()) {
                std::cout << "Breakdown:";
                for (const auto& entry : perKind) {
                    std::cout << " " << kindCode(entry.first) << "=" << entry.second;
                }
                std::cout << std::endl;
            }
        }
        return 0;
    }

    if (command == "replace") {
        if (!fromVar || !toVar) {
            std::cerr << "--from and --to are required for replace" << std::endl;
            return 1;
        }

        int modifiedCount = 0;
        std::vector<LogEntry> logEntries;
        for (auto& occ : occurrences) {
            if (currentVar(occ) == *fromVar) {
                int rawBefore = occ.valuePtr ? *occ.valuePtr : 0;
                int rawAfter = (occ.encoding == ValueEncoding::Direct)
                                   ? *toVar
                                   : (*toVar * 10 + compositeRemainder(occ));
                logEntries.push_back({*fromVar,
                                      *toVar,
                                      occ.seqIndex,
                                      sequenceLabel(*occ.sequence, occ.seqIndex),
                                      occ.frameIndex,
                                      nodeLabel(occ),
                                      rawBefore,
                                      rawAfter});
                ++modifiedCount;
                if (!dryRun) {
                    applyVarChange(occ, *toVar);
                }
            }
        }

        if (modifiedCount == 0) {
            std::cout << "No occurrences with var " << *fromVar << " found." << std::endl;
            return 0;
        }

        std::cout << modifiedCount << " occurrence(s) "
                  << (dryRun ? "would be" : "were")
                  << " updated from var " << *fromVar << " to " << *toVar << "." << std::endl;

        fs::path output = inPlace ? inputPath : (outPath ? *outPath : defaultOutputPath(inputPath));

        if (dryRun) {
            std::cout << "Dry run only. No file was written." << std::endl;
            if (!logEntries.empty()) {
                std::cout << "Log file generation is skipped during dry runs." << std::endl;
            }
            return 0;
        }

        data.save(output.string().c_str());
        std::cout << "Updated file written to " << output << std::endl;

        if (!disableLog && !logEntries.empty()) {
            fs::path resolvedLog = logPath ? *logPath : defaultLogPath(output);
            writeLogFile(resolvedLog, output, logEntries);
            std::cout << "Logged " << logEntries.size()
                      << " occurrence(s) to " << resolvedLog << std::endl;
        } else if (disableLog) {
            std::cout << "Log file generation disabled for this run." << std::endl;
        }
        return 0;
    }

    std::cerr << "Unknown command: " << command << std::endl;
    printUsage();
    return 1;
}
