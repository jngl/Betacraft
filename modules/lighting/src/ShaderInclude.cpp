#include "lighting/ShaderInclude.hpp"

#include <cctype>

namespace betacraft::lighting {

namespace {

enum class LineKind { Blank, Directive, Code };

struct LineInfo {
    LineKind kind{LineKind::Blank};
    std::string_view directive; // Name after '#' (e.g. "version"), Directive lines only
    size_t next{0};             // Offset just past this line's '\n' (or the source size)
    bool endsInComment{false};  // Line ends inside a /* */ comment: not a safe splice point
};

[[nodiscard]] bool isIdentChar(char c) noexcept {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// Classifies the line starting at `begin`, ignoring comments. `inComment` carries the
// block-comment state across lines, so a `#version` inside /* */ is never mistaken for one.
[[nodiscard]] LineInfo scanLine(std::string_view src, size_t begin, bool& inComment) noexcept {
    LineInfo info;
    size_t eol = src.find('\n', begin);
    info.next = eol == std::string_view::npos ? src.size() : eol + 1;
    if (eol == std::string_view::npos) {
        eol = src.size();
    }
    bool seenToken = false;
    for (size_t p = begin; p < eol; ++p) {
        if (inComment) {
            if (src.compare(p, 2, "*/") == 0) {
                inComment = false;
                ++p;
            }
            continue;
        }
        if (src.compare(p, 2, "/*") == 0) {
            inComment = true;
            ++p;
            continue;
        }
        if (src.compare(p, 2, "//") == 0) {
            break;
        }
        const char c = src[p];
        if (c == ' ' || c == '\t' || c == '\r') {
            continue;
        }
        if (!seenToken) {
            seenToken = true;
            if (c != '#') {
                info.kind = LineKind::Code;
                break;
            }
            // `#`, optional blanks, then the directive name (`# version` is valid GLSL).
            info.kind = LineKind::Directive;
            size_t n = p + 1;
            while (n < eol && (src[n] == ' ' || src[n] == '\t')) {
                ++n;
            }
            size_t e = n;
            while (e < eol && isIdentChar(src[e])) {
                ++e;
            }
            info.directive = src.substr(n, e - n);
            p = e - 1;
        }
    }
    info.endsInComment = inComment;
    return info;
}

// Where the include goes: after `#version` and every `#extension` that precedes the first
// line of code (GLSL requires #extension before any non-preprocessor token, and the include
// starts with declarations). npos when the source has no leading #version.
[[nodiscard]] size_t findSplicePoint(std::string_view src) noexcept {
    bool inComment = false;
    size_t pos = 0;
    size_t splice = std::string_view::npos;
    bool versionSeen = false;
    while (pos < src.size()) {
        const LineInfo line = scanLine(src, pos, inComment);
        pos = line.next;
        if (line.kind == LineKind::Blank) {
            if (versionSeen && splice == std::string_view::npos && !line.endsInComment) {
                splice = line.next; // #version line ended inside a comment that closes here
            }
            continue;
        }
        if (!versionSeen) {
            // #version must be the first directive/token; anything else means "no version".
            if (line.kind != LineKind::Directive || line.directive != "version") {
                return std::string_view::npos;
            }
            versionSeen = true;
        } else if (line.kind == LineKind::Code) {
            break;
        } else if (line.directive != "extension") {
            continue;
        }
        if (!line.endsInComment) {
            splice = line.next;
        }
    }
    return splice;
}

} // namespace

std::string injectShaderInclude(std::string_view shaderSrc, std::string_view include) {
    if (include.empty()) {
        return std::string(shaderSrc);
    }
    std::string block(include);
    if (block.back() != '\n') {
        block.push_back('\n');
    }

    const size_t splice = findSplicePoint(shaderSrc);
    if (splice == std::string_view::npos) {
        return block + std::string(shaderSrc);
    }

    std::string out;
    out.reserve(shaderSrc.size() + block.size() + 1);
    out.append(shaderSrc.substr(0, splice));
    if (out.back() != '\n') {
        out.push_back('\n'); // #version on the last line without a newline
    }
    out.append(block);
    out.append(shaderSrc.substr(splice));
    return out;
}

} // namespace betacraft::lighting
