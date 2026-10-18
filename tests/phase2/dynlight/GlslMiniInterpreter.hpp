#pragma once

// Test-only: a small tree-walking interpreter for the GLSL 3.30 subset used by
// shaders/dynamic_lights.glsl. It executes the shader's OWN code (not a C++ restatement of it)
// so tests can compare dynTileIndex / dynTileMask / computeDynamicLight with the CPU culler,
// packer and DynamicLightFrame::shade().
//
// It is deliberately strict, so it also catches GLSL that would not compile or would be
// undefined on a driver:
//   - GLSL 3.30 implicit conversions only (int/uint -> float); int <-> uint mixing is an error;
//   - bitwise/shift/% operators on integers only, shift counts in [0, 32);
//   - out-of-range array / vector component indexing is an error (undefined on GPUs);
//   - NaN reaching min/max/clamp is an error (implementation-defined in GLSL);
//   - float -> int/uint conversion of out-of-range values is an error;
//   - assignment to uniforms, recursion, missing returns and unknown identifiers are errors.
// Unsupported syntax (matrices, arrays declared in functions, #if...) is rejected loudly.

#include "lighting/Std140.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dynlight_test::glsl {

struct GlslError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] inline void fail(const std::string& msg) {
    throw GlslError(msg);
}

enum class Base { Void, Float, Int, UInt, Bool, Struct, Array };

// Scalars/vectors use `n` components of f (Float) or i (Int/UInt/Bool, int32/uint32 semantics).
// Structs and arrays store their members/elements in `elems` (struct field names in fieldNames).
struct Value {
    Base base{Base::Void};
    int n{1};
    std::array<float, 4> f{};
    std::array<int64_t, 4> i{};
    std::string structName;
    std::vector<std::string> fieldNames;
    std::vector<Value> elems;
};

[[nodiscard]] inline bool isInteger(Base b) noexcept {
    return b == Base::Int || b == Base::UInt;
}
[[nodiscard]] inline bool isNumeric(Base b) noexcept {
    return b == Base::Float || isInteger(b);
}
[[nodiscard]] inline bool isVectorLike(const Value& v) noexcept {
    return isNumeric(v.base) || v.base == Base::Bool;
}

[[nodiscard]] inline int64_t wrapInt(int64_t v) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(v) & 0xFFFFFFFFull));
}
[[nodiscard]] inline int64_t wrapUInt(int64_t v) noexcept {
    return static_cast<int64_t>(static_cast<uint64_t>(v) & 0xFFFFFFFFull);
}

[[nodiscard]] inline std::optional<std::pair<Base, int>> basicType(std::string_view name) {
    static const std::map<std::string, std::pair<Base, int>, std::less<>> table{
        {"float", {Base::Float, 1}}, {"vec2", {Base::Float, 2}},  {"vec3", {Base::Float, 3}},
        {"vec4", {Base::Float, 4}},  {"int", {Base::Int, 1}},     {"ivec2", {Base::Int, 2}},
        {"ivec3", {Base::Int, 3}},   {"ivec4", {Base::Int, 4}},   {"uint", {Base::UInt, 1}},
        {"uvec2", {Base::UInt, 2}},  {"uvec3", {Base::UInt, 3}},  {"uvec4", {Base::UInt, 4}},
        {"bool", {Base::Bool, 1}},   {"bvec2", {Base::Bool, 2}},  {"bvec3", {Base::Bool, 3}},
        {"bvec4", {Base::Bool, 4}},
    };
    if (auto it = table.find(name); it != table.end()) {
        return it->second;
    }
    return std::nullopt;
}

[[nodiscard]] inline std::string typeNameOf(const Value& v) {
    const std::string n = std::to_string(v.n);
    switch (v.base) {
        case Base::Void: return "void";
        case Base::Float: return v.n == 1 ? "float" : "vec" + n;
        case Base::Int: return v.n == 1 ? "int" : "ivec" + n;
        case Base::UInt: return v.n == 1 ? "uint" : "uvec" + n;
        case Base::Bool: return v.n == 1 ? "bool" : "bvec" + n;
        case Base::Struct: return v.structName;
        case Base::Array:
            return (v.elems.empty() ? std::string("?") : typeNameOf(v.elems[0])) + "[" +
                   std::to_string(v.elems.size()) + "]";
    }
    return "?";
}

[[nodiscard]] inline Value makeScalar(Base b, int64_t v) {
    Value r;
    r.base = b;
    r.i[0] = b == Base::Int ? wrapInt(v) : (b == Base::UInt ? wrapUInt(v) : (v != 0 ? 1 : 0));
    return r;
}
[[nodiscard]] inline Value floatv(float x) {
    Value r;
    r.base = Base::Float;
    r.f[0] = x;
    return r;
}
[[nodiscard]] inline Value intv(int32_t x) {
    return makeScalar(Base::Int, x);
}
[[nodiscard]] inline Value uintv(uint32_t x) {
    return makeScalar(Base::UInt, x);
}
[[nodiscard]] inline Value boolv(bool x) {
    return makeScalar(Base::Bool, x ? 1 : 0);
}
[[nodiscard]] inline Value vec2v(const glm::vec2& v) {
    Value r = floatv(v.x);
    r.n = 2;
    r.f[1] = v.y;
    return r;
}
[[nodiscard]] inline Value vec3v(const glm::vec3& v) {
    Value r = floatv(v.x);
    r.n = 3;
    r.f[1] = v.y;
    r.f[2] = v.z;
    return r;
}

[[nodiscard]] inline float asFloat(const Value& v) {
    if (v.base != Base::Float || v.n != 1) {
        fail("expected float, got " + typeNameOf(v));
    }
    return v.f[0];
}
[[nodiscard]] inline int64_t asInt(const Value& v) {
    if (v.base != Base::Int || v.n != 1) {
        fail("expected int, got " + typeNameOf(v));
    }
    return v.i[0];
}
[[nodiscard]] inline uint32_t asUInt(const Value& v) {
    if (v.base != Base::UInt || v.n != 1) {
        fail("expected uint, got " + typeNameOf(v));
    }
    return static_cast<uint32_t>(v.i[0]);
}
[[nodiscard]] inline glm::vec3 asVec3(const Value& v) {
    if (v.base != Base::Float || v.n != 3) {
        fail("expected vec3, got " + typeNameOf(v));
    }
    return {v.f[0], v.f[1], v.f[2]};
}

// --- Lexer --------------------------------------------------------------------------------

struct Token {
    enum class Kind { Ident, Number, Punct, End } kind{Kind::End};
    std::string text;
};

namespace detail {

// Replaces comments by blanks (newlines kept so preprocessor lines stay intact).
[[nodiscard]] inline std::string stripComments(std::string_view src) {
    std::string out;
    out.reserve(src.size());
    size_t p = 0;
    while (p < src.size()) {
        if (src.compare(p, 2, "//") == 0) {
            while (p < src.size() && src[p] != '\n') {
                ++p;
            }
        } else if (src.compare(p, 2, "/*") == 0) {
            const size_t end = src.find("*/", p + 2);
            if (end == std::string_view::npos) {
                fail("unterminated block comment");
            }
            for (size_t k = p; k < end + 2; ++k) {
                out.push_back(src[k] == '\n' ? '\n' : ' ');
            }
            p = end + 2;
        } else {
            out.push_back(src[p++]);
        }
    }
    return out;
}

inline void lexLine(std::string_view s, std::vector<Token>& out,
                    const std::map<std::string, std::vector<Token>>& macros) {
    static const std::array<std::string_view, 21> multi{"<<=", ">>=", "<<", ">>", "<=", ">=", "==",
                                                        "!=",  "&&",  "||", "^^", "++", "--", "+=",
                                                        "-=",  "*=",  "/=", "%=", "&=", "|=", "^="};
    size_t p = 0;
    while (p < s.size()) {
        const char c = s[p];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++p;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t e = p;
            while (e < s.size() && (std::isalnum(static_cast<unsigned char>(s[e])) || s[e] == '_')) {
                ++e;
            }
            std::string id(s.substr(p, e - p));
            if (auto m = macros.find(id); m != macros.end()) {
                out.insert(out.end(), m->second.begin(), m->second.end());
            } else {
                out.push_back({Token::Kind::Ident, id});
            }
            p = e;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && p + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[p + 1])))) {
            size_t e = p;
            while (e < s.size()) {
                const char d = s[e];
                if (std::isalnum(static_cast<unsigned char>(d)) || d == '.') {
                    ++e;
                } else if ((d == '+' || d == '-') && (s[e - 1] == 'e' || s[e - 1] == 'E')) {
                    ++e;
                } else {
                    break;
                }
            }
            out.push_back({Token::Kind::Number, std::string(s.substr(p, e - p))});
            p = e;
            continue;
        }
        bool matched = false;
        for (std::string_view op : multi) {
            if (s.compare(p, op.size(), op) == 0) {
                out.push_back({Token::Kind::Punct, std::string(op)});
                p += op.size();
                matched = true;
                break;
            }
        }
        if (matched) {
            continue;
        }
        if (std::string_view("+-*/%<>=!&|^~?:;,.(){}[]").find(c) == std::string_view::npos) {
            fail(std::string("unexpected character '") + c + "'");
        }
        out.push_back({Token::Kind::Punct, std::string(1, c)});
        ++p;
    }
}

} // namespace detail

// Handles `#define NAME tokens...` (object-like only); other directives except #version and
// #extension are rejected so unsupported preprocessing never goes unnoticed.
[[nodiscard]] inline std::vector<Token> tokenize(std::string_view source) {
    const std::string src = detail::stripComments(source);
    std::map<std::string, std::vector<Token>> macros;
    std::vector<Token> out;
    size_t lineStart = 0;
    while (lineStart <= src.size()) {
        size_t eol = src.find('\n', lineStart);
        if (eol == std::string::npos) {
            eol = src.size();
        }
        const std::string_view line(src.data() + lineStart, eol - lineStart);
        const size_t first = line.find_first_not_of(" \t\r");
        if (first != std::string_view::npos && line[first] == '#') {
            std::vector<Token> dir;
            detail::lexLine(line.substr(first + 1), dir, {});
            if (dir.empty() || dir[0].kind != Token::Kind::Ident) {
                fail("malformed preprocessor directive");
            }
            if (dir[0].text == "define") {
                if (dir.size() < 2 || dir[1].kind != Token::Kind::Ident) {
                    fail("malformed #define");
                }
                if (line.find(dir[1].text + "(") != std::string_view::npos) {
                    fail("function-like macros are not supported");
                }
                std::vector<Token> body;
                for (size_t k = 2; k < dir.size(); ++k) {
                    if (dir[k].kind == Token::Kind::Ident) {
                        if (auto m = macros.find(dir[k].text); m != macros.end()) {
                            body.insert(body.end(), m->second.begin(), m->second.end());
                            continue;
                        }
                    }
                    body.push_back(dir[k]);
                }
                macros[dir[1].text] = body;
            } else if (dir[0].text != "version" && dir[0].text != "extension") {
                fail("unsupported preprocessor directive #" + dir[0].text);
            }
        } else {
            detail::lexLine(line, out, macros);
        }
        lineStart = eol + 1;
    }
    out.push_back({Token::Kind::End, ""});
    return out;
}

// --- AST ----------------------------------------------------------------------------------

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;
struct Expr {
    enum class K { Lit, Var, Bin, Un, PreInc, PostInc, Call, Member, Index, Assign, Cond } k{K::Lit};
    std::string s; // operator, identifier, member name or callee
    Value lit;
    std::vector<ExprPtr> a;
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;
struct Stmt {
    enum class K { Block, Decl, Expr, If, For, While, Return, Break, Continue, Empty } k{K::Empty};
    std::string type;
    std::string name;
    ExprPtr e;    // Decl initializer, expression, condition, return value
    ExprPtr step; // For
    StmtPtr init; // For
    StmtPtr then; // If / loop body
    StmtPtr els;  // If
    std::vector<StmtPtr> body;
};

struct Decl {
    std::string type;
    std::string name;
    std::optional<size_t> arraySize;
};

struct StructDecl {
    std::string name;
    std::vector<Decl> members;
};

struct BlockDecl {
    std::string layout;
    std::string name;
    std::vector<Decl> members;
};

struct Function {
    std::string ret;
    std::string name;
    std::vector<Decl> params;
    StmtPtr body;
};

// --- Program ------------------------------------------------------------------------------

class Program {
public:
    explicit Program(std::string_view source) : m_tok(tokenize(source)) {
        while (peek().kind != Token::Kind::End) {
            parseTopLevel();
        }
    }

    [[nodiscard]] const BlockDecl* block(std::string_view name) const {
        for (const BlockDecl& b : m_blocks) {
            if (b.name == name) {
                return &b;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool hasFunction(std::string_view name) const {
        return std::any_of(m_functions.begin(), m_functions.end(),
                           [&](const auto& f) { return f->name == name; });
    }

    // Fills the uniform block's members from std140 bytes (offsets derived from the GLSL
    // declarations themselves through std140::LayoutBuilder).
    void bindUniformBlock(std::string_view blockName, std::span<const std::byte> bytes) {
        const BlockDecl* b = block(blockName);
        if (b == nullptr) {
            fail("no uniform block " + std::string(blockName));
        }
        if (b->layout != "std140") {
            fail("only std140 blocks can be bound");
        }
        betacraft::lighting::std140::LayoutBuilder lb;
        for (const Decl& m : b->members) {
            const size_t count = m.arraySize.value_or(1);
            Value v;
            if (const StructDecl* sd = findStruct(m.type)) {
                const auto [fieldOffsets, structSize] = structLayout(*sd);
                const size_t offset = lb.addStructArray(structSize, count);
                const size_t stride = betacraft::lighting::std140::alignUp(structSize, betacraft::lighting::std140::kVec4Alignment);
                std::vector<Value> elems;
                for (size_t k = 0; k < count; ++k) {
                    Value s;
                    s.base = Base::Struct;
                    s.structName = sd->name;
                    for (size_t fi = 0; fi < sd->members.size(); ++fi) {
                        s.fieldNames.push_back(sd->members[fi].name);
                        s.elems.push_back(readBasic(bytes, offset + k * stride + fieldOffsets[fi], sd->members[fi].type));
                    }
                    elems.push_back(std::move(s));
                }
                v = m.arraySize ? makeArray(std::move(elems)) : std::move(elems[0]);
            } else {
                const auto t = std140Type(m.type);
                if (m.arraySize) {
                    const size_t offset = lb.addArray(t, count);
                    std::vector<Value> elems;
                    for (size_t k = 0; k < count; ++k) {
                        elems.push_back(readBasic(bytes, offset + k * betacraft::lighting::std140::arrayStride(t), m.type));
                    }
                    v = makeArray(std::move(elems));
                } else {
                    v = readBasic(bytes, lb.add(t), m.type);
                }
            }
            m_globals[m.name] = std::move(v);
        }
        if (lb.structSize() > bytes.size()) {
            fail("uniform buffer smaller than the declared block");
        }
    }

    Value call(std::string_view name, std::vector<Value> args) {
        m_steps = 0;
        return callFunction(std::string(name), std::move(args));
    }

private:
    // --- parsing ------------------------------------------------------------------------
    [[nodiscard]] const Token& peek(size_t ahead = 0) const {
        return m_tok[std::min(m_pos + ahead, m_tok.size() - 1)];
    }
    const Token& next() {
        const Token& t = m_tok[m_pos];
        if (m_pos + 1 < m_tok.size()) {
            ++m_pos;
        }
        return t;
    }
    bool accept(std::string_view text) {
        if (peek().kind != Token::Kind::End && peek().text == text &&
            (peek().kind == Token::Kind::Punct || peek().kind == Token::Kind::Ident)) {
            next();
            return true;
        }
        return false;
    }
    void expect(std::string_view text) {
        if (!accept(text)) {
            fail("expected '" + std::string(text) + "' but found '" + peek().text + "'");
        }
    }
    std::string ident() {
        if (peek().kind != Token::Kind::Ident) {
            fail("expected identifier but found '" + peek().text + "'");
        }
        return next().text;
    }
    [[nodiscard]] bool isTypeName(const std::string& s) const {
        return basicType(s).has_value() || s == "void" || findStruct(s) != nullptr;
    }
    [[nodiscard]] const StructDecl* findStruct(std::string_view name) const {
        for (const StructDecl& s : m_structs) {
            if (s.name == name) {
                return &s;
            }
        }
        return nullptr;
    }

    std::vector<Decl> parseMemberList() {
        std::vector<Decl> members;
        expect("{");
        while (!accept("}")) {
            Decl d;
            d.type = ident();
            if (!isTypeName(d.type) || d.type == "void") {
                fail("unknown member type " + d.type);
            }
            d.name = ident();
            if (accept("[")) {
                if (peek().kind != Token::Kind::Number) {
                    fail("array size must be a constant");
                }
                d.arraySize = std::stoul(next().text);
                expect("]");
            }
            expect(";");
            members.push_back(d);
        }
        expect(";");
        return members;
    }

    void parseTopLevel() {
        if (accept("struct")) {
            StructDecl s;
            s.name = ident();
            s.members = parseMemberList();
            m_structs.push_back(std::move(s));
            return;
        }
        if (accept("layout")) {
            expect("(");
            BlockDecl b;
            b.layout = ident();
            expect(")");
            expect("uniform");
            b.name = ident();
            b.members = parseMemberList();
            m_blocks.push_back(std::move(b));
            return;
        }
        if (peek().text == "uniform") {
            fail("uniform declarations outside a block are not supported");
        }
        auto fn = std::make_unique<Function>();
        fn->ret = ident();
        if (!isTypeName(fn->ret)) {
            fail("unknown type " + fn->ret);
        }
        fn->name = ident();
        expect("(");
        if (!accept(")")) {
            if (peek().text == "void" && peek(1).text == ")") {
                next();
            } else {
                do {
                    accept("in");
                    if (peek().text == "out" || peek().text == "inout") {
                        fail("out/inout parameters are not supported");
                    }
                    Decl p;
                    p.type = ident();
                    if (!isTypeName(p.type) || p.type == "void") {
                        fail("bad parameter type " + p.type);
                    }
                    p.name = ident();
                    fn->params.push_back(p);
                } while (accept(","));
            }
            expect(")");
        }
        if (peek().text != "{") {
            fail("function prototypes are not supported");
        }
        fn->body = parseStatement();
        m_functions.push_back(std::move(fn));
    }

    StmtPtr parseDeclOrExpr() {
        auto st = std::make_unique<Stmt>();
        accept("const");
        if (peek().kind == Token::Kind::Ident && isTypeName(peek().text) && peek(1).kind == Token::Kind::Ident) {
            st->k = Stmt::K::Decl;
            st->type = ident();
            st->name = ident();
            if (peek().text == "[" || peek().text == ",") {
                fail("local arrays / declaration lists are not supported");
            }
            if (accept("=")) {
                st->e = parseAssign();
            }
        } else {
            st->k = Stmt::K::Expr;
            st->e = parseExpr();
        }
        return st;
    }

    StmtPtr parseStatement() {
        auto st = std::make_unique<Stmt>();
        if (accept("{")) {
            st->k = Stmt::K::Block;
            while (!accept("}")) {
                if (peek().kind == Token::Kind::End) {
                    fail("unterminated block");
                }
                st->body.push_back(parseStatement());
            }
            return st;
        }
        if (accept(";")) {
            return st;
        }
        if (accept("if")) {
            st->k = Stmt::K::If;
            expect("(");
            st->e = parseExpr();
            expect(")");
            st->then = parseStatement();
            if (accept("else")) {
                st->els = parseStatement();
            }
            return st;
        }
        if (accept("for")) {
            st->k = Stmt::K::For;
            expect("(");
            if (!accept(";")) {
                st->init = parseDeclOrExpr();
                expect(";");
            }
            if (!accept(";")) {
                st->e = parseExpr();
                expect(";");
            }
            if (!accept(")")) {
                st->step = parseExpr();
                expect(")");
            }
            st->then = parseStatement();
            return st;
        }
        if (accept("while")) {
            st->k = Stmt::K::While;
            expect("(");
            st->e = parseExpr();
            expect(")");
            st->then = parseStatement();
            return st;
        }
        if (accept("return")) {
            st->k = Stmt::K::Return;
            if (!accept(";")) {
                st->e = parseExpr();
                expect(";");
            }
            return st;
        }
        if (accept("break")) {
            st->k = Stmt::K::Break;
            expect(";");
            return st;
        }
        if (accept("continue")) {
            st->k = Stmt::K::Continue;
            expect(";");
            return st;
        }
        StmtPtr d = parseDeclOrExpr();
        expect(";");
        return d;
    }

    static ExprPtr node(Expr::K k, std::string s) {
        auto e = std::make_unique<Expr>();
        e->k = k;
        e->s = std::move(s);
        return e;
    }

    ExprPtr parseExpr() {
        ExprPtr e = parseAssign();
        if (peek().text == ",") {
            fail("comma operator is not supported");
        }
        return e;
    }

    ExprPtr parseAssign() {
        ExprPtr lhs = parseCond();
        static const std::set<std::string> ops{"=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "|=", "^="};
        if (peek().kind == Token::Kind::Punct && ops.count(peek().text)) {
            ExprPtr e = node(Expr::K::Assign, next().text);
            e->a.push_back(std::move(lhs));
            e->a.push_back(parseAssign());
            return e;
        }
        return lhs;
    }

    ExprPtr parseCond() {
        ExprPtr c = parseBinary(0);
        if (accept("?")) {
            ExprPtr e = node(Expr::K::Cond, "?");
            e->a.push_back(std::move(c));
            e->a.push_back(parseAssign());
            expect(":");
            e->a.push_back(parseAssign());
            return e;
        }
        return c;
    }

    ExprPtr parseBinary(int level) {
        static const std::vector<std::vector<std::string>> levels{
            {"||"}, {"^^"}, {"&&"}, {"|"}, {"^"}, {"&"}, {"==", "!="}, {"<", ">", "<=", ">="}, {"<<", ">>"},
            {"+", "-"}, {"*", "/", "%"}};
        if (level == static_cast<int>(levels.size())) {
            return parseUnary();
        }
        ExprPtr lhs = parseBinary(level + 1);
        for (;;) {
            const auto& ops = levels[static_cast<size_t>(level)];
            if (peek().kind != Token::Kind::Punct || std::find(ops.begin(), ops.end(), peek().text) == ops.end()) {
                return lhs;
            }
            ExprPtr e = node(Expr::K::Bin, next().text);
            e->a.push_back(std::move(lhs));
            e->a.push_back(parseBinary(level + 1));
            lhs = std::move(e);
        }
    }

    ExprPtr parseUnary() {
        if (peek().kind == Token::Kind::Punct) {
            const std::string t = peek().text;
            if (t == "-" || t == "+" || t == "!" || t == "~") {
                next();
                ExprPtr e = node(Expr::K::Un, t);
                e->a.push_back(parseUnary());
                return e;
            }
            if (t == "++" || t == "--") {
                next();
                ExprPtr e = node(Expr::K::PreInc, t);
                e->a.push_back(parseUnary());
                return e;
            }
        }
        return parsePostfix();
    }

    ExprPtr parsePostfix() {
        ExprPtr e = parsePrimary();
        for (;;) {
            if (accept("[")) {
                ExprPtr ix = node(Expr::K::Index, "[]");
                ix->a.push_back(std::move(e));
                ix->a.push_back(parseExpr());
                expect("]");
                e = std::move(ix);
            } else if (accept(".")) {
                ExprPtr m = node(Expr::K::Member, ident());
                m->a.push_back(std::move(e));
                e = std::move(m);
            } else if (peek().text == "++" || peek().text == "--") {
                ExprPtr p = node(Expr::K::PostInc, next().text);
                p->a.push_back(std::move(e));
                e = std::move(p);
            } else {
                return e;
            }
        }
    }

    static Value parseNumber(const std::string& text) {
        std::string t = text;
        const bool hex = t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X');
        const bool uns = !t.empty() && (t.back() == 'u' || t.back() == 'U');
        if (uns) {
            t.pop_back();
        }
        const bool isFloat = !hex && !uns &&
                             (t.find_first_of(".eE") != std::string::npos || t.back() == 'f' || t.back() == 'F');
        if (isFloat) {
            if (t.back() == 'f' || t.back() == 'F') {
                t.pop_back();
            }
            size_t used = 0;
            const float v = std::stof(t, &used);
            if (used != t.size()) {
                fail("bad float literal " + text);
            }
            return floatv(v);
        }
        size_t used = 0;
        const unsigned long long v = std::stoull(t, &used, hex ? 16 : (t.size() > 1 && t[0] == '0' ? 8 : 10));
        if (used != t.size() || v > 0xFFFFFFFFull) {
            fail("bad integer literal " + text);
        }
        return uns ? uintv(static_cast<uint32_t>(v)) : intv(static_cast<int32_t>(static_cast<uint32_t>(v)));
    }

    ExprPtr parsePrimary() {
        if (accept("(")) {
            ExprPtr e = parseExpr();
            expect(")");
            return e;
        }
        if (peek().kind == Token::Kind::Number) {
            ExprPtr e = node(Expr::K::Lit, peek().text);
            e->lit = parseNumber(next().text);
            return e;
        }
        if (peek().kind != Token::Kind::Ident) {
            fail("unexpected token '" + peek().text + "'");
        }
        const std::string id = next().text;
        if (id == "true" || id == "false") {
            ExprPtr e = node(Expr::K::Lit, id);
            e->lit = boolv(id == "true");
            return e;
        }
        if (accept("(")) {
            ExprPtr c = node(Expr::K::Call, id);
            if (!accept(")")) {
                do {
                    c->a.push_back(parseAssign());
                } while (accept(","));
                expect(")");
            }
            return c;
        }
        return node(Expr::K::Var, id);
    }

    // --- layout helpers -----------------------------------------------------------------
    static betacraft::lighting::std140::Type std140Type(const std::string& t) {
        using betacraft::lighting::std140::Type;
        static const std::map<std::string, Type> table{{"float", Type::Float}, {"int", Type::Int},
                                                       {"uint", Type::UInt},   {"vec2", Type::Vec2},
                                                       {"vec3", Type::Vec3},   {"vec4", Type::Vec4},
                                                       {"ivec4", Type::IVec4}, {"uvec4", Type::UVec4}};
        if (auto it = table.find(t); it != table.end()) {
            return it->second;
        }
        fail("type not supported in uniform blocks by this interpreter: " + t);
    }

    std::pair<std::vector<size_t>, size_t> structLayout(const StructDecl& sd) const {
        betacraft::lighting::std140::LayoutBuilder b;
        std::vector<size_t> offsets;
        for (const Decl& m : sd.members) {
            if (m.arraySize || findStruct(m.type)) {
                fail("nested arrays/structs in uniform structs are not supported");
            }
            offsets.push_back(b.add(std140Type(m.type)));
        }
        return {offsets, b.structSize()};
    }

    static Value readBasic(std::span<const std::byte> bytes, size_t offset, const std::string& type) {
        const auto t = basicType(type);
        if (!t) {
            fail("cannot read " + type);
        }
        Value v;
        v.base = t->first;
        v.n = t->second;
        for (int k = 0; k < v.n; ++k) {
            const size_t at = offset + static_cast<size_t>(k) * 4u;
            if (at + 4u > bytes.size()) {
                fail("uniform read out of buffer bounds");
            }
            if (v.base == Base::Float) {
                std::memcpy(&v.f[static_cast<size_t>(k)], bytes.data() + at, 4);
            } else {
                uint32_t raw = 0;
                std::memcpy(&raw, bytes.data() + at, 4);
                v.i[static_cast<size_t>(k)] = v.base == Base::Int ? wrapInt(raw) : (v.base == Base::UInt ? raw : raw != 0);
            }
        }
        return v;
    }

    static Value makeArray(std::vector<Value> elems) {
        Value a;
        a.base = Base::Array;
        a.elems = std::move(elems);
        return a;
    }

    Value defaultOf(const std::string& type) const {
        if (auto t = basicType(type)) {
            Value v;
            v.base = t->first;
            v.n = t->second;
            return v;
        }
        if (const StructDecl* sd = findStruct(type)) {
            Value s;
            s.base = Base::Struct;
            s.structName = sd->name;
            for (const Decl& m : sd->members) {
                s.fieldNames.push_back(m.name);
                s.elems.push_back(defaultOf(m.type));
            }
            return s;
        }
        fail("unknown type " + type);
    }

    // --- semantics ----------------------------------------------------------------------
    static Value toFloat(const Value& v) {
        Value r = v;
        r.base = Base::Float;
        for (int k = 0; k < v.n; ++k) {
            r.f[static_cast<size_t>(k)] = static_cast<float>(v.i[static_cast<size_t>(k)]);
        }
        return r;
    }

    // GLSL 3.30 implicit conversions: only int/uint (and vectors) -> float (and vectors).
    static Value implicitTo(const Value& v, const std::string& target) {
        if (typeNameOf(v) == target) {
            return v;
        }
        if (auto t = basicType(target); t && t->first == Base::Float && isInteger(v.base) && v.n == t->second) {
            return toFloat(v);
        }
        fail("cannot implicitly convert " + typeNameOf(v) + " to " + target);
    }

    static void unify(Value& a, Value& b, const std::string& op) {
        if (a.base == b.base) {
            return;
        }
        if (a.base == Base::Float && isInteger(b.base)) {
            b = toFloat(b);
        } else if (b.base == Base::Float && isInteger(a.base)) {
            a = toFloat(a);
        } else {
            fail("type mismatch for '" + op + "': " + typeNameOf(a) + " vs " + typeNameOf(b));
        }
    }

    static int broadcastSize(const Value& a, const Value& b, const std::string& op) {
        if (a.n != b.n && a.n != 1 && b.n != 1) {
            fail("size mismatch for '" + op + "': " + typeNameOf(a) + " vs " + typeNameOf(b));
        }
        return std::max(a.n, b.n);
    }

    static size_t ci(const Value& v, int k) {
        return v.n == 1 ? 0u : static_cast<size_t>(k);
    }

    static void requireNotNaN(const Value& v, const char* fn) {
        if (v.base != Base::Float) {
            return;
        }
        for (int k = 0; k < v.n; ++k) {
            if (std::isnan(v.f[static_cast<size_t>(k)])) {
                fail(std::string("NaN passed to ") + fn + "(): implementation-defined in GLSL");
            }
        }
    }

    static Value binary(const std::string& op, Value a, Value b) {
        if (!isVectorLike(a) || !isVectorLike(b)) {
            fail("operator '" + op + "' on " + typeNameOf(a) + " / " + typeNameOf(b));
        }
        if (op == "==" || op == "!=") {
            if (a.base != Base::Bool || b.base != Base::Bool) {
                unify(a, b, op);
            }
            if (typeNameOf(a) != typeNameOf(b)) {
                fail("type mismatch for '" + op + "'");
            }
            bool eq = true;
            for (int k = 0; k < a.n; ++k) {
                const size_t c = static_cast<size_t>(k);
                eq = eq && (a.base == Base::Float ? a.f[c] == b.f[c] : a.i[c] == b.i[c]);
            }
            return boolv(op == "==" ? eq : !eq);
        }
        if (a.base == Base::Bool || b.base == Base::Bool) {
            fail("operator '" + op + "' on bool");
        }
        if (op == "<" || op == ">" || op == "<=" || op == ">=") {
            if (a.n != 1 || b.n != 1) {
                fail("relational operator on vectors");
            }
            unify(a, b, op);
            const double x = a.base == Base::Float ? a.f[0] : static_cast<double>(a.i[0]);
            const double y = b.base == Base::Float ? b.f[0] : static_cast<double>(b.i[0]);
            return boolv(op == "<" ? x < y : op == ">" ? x > y : op == "<=" ? x <= y : x >= y);
        }
        if (op == "<<" || op == ">>") {
            if (!isInteger(a.base) || !isInteger(b.base)) {
                fail("shift on non-integer");
            }
            if (b.n != 1 && b.n != a.n) {
                fail("shift size mismatch");
            }
            Value r = a;
            for (int k = 0; k < a.n; ++k) {
                const int64_t s = b.i[ci(b, k)];
                if (s < 0 || s >= 32) {
                    fail("shift count out of range (undefined): " + std::to_string(s));
                }
                const size_t c = static_cast<size_t>(k);
                if (op == "<<") {
                    const uint64_t bits = static_cast<uint64_t>(wrapUInt(a.i[c])) << s;
                    r.i[c] = a.base == Base::Int ? wrapInt(static_cast<int64_t>(bits)) : wrapUInt(static_cast<int64_t>(bits));
                } else {
                    r.i[c] = a.base == Base::Int ? (a.i[c] >> s) : wrapUInt(a.i[c] >> s);
                }
            }
            return r;
        }
        if (op == "&" || op == "|" || op == "^" || op == "%") {
            if (!isInteger(a.base) || a.base != b.base) {
                fail("operator '" + op + "' needs matching integer operands: " + typeNameOf(a) + " vs " + typeNameOf(b));
            }
            const int n = broadcastSize(a, b, op);
            Value r = a.n >= b.n ? a : b;
            for (int k = 0; k < n; ++k) {
                const int64_t x = a.i[ci(a, k)];
                const int64_t y = b.i[ci(b, k)];
                int64_t v = 0;
                if (op == "%") {
                    if (y == 0 || x < 0 || y < 0) {
                        fail("% with zero or negative operands is undefined");
                    }
                    v = x % y;
                } else {
                    const uint64_t ux = static_cast<uint64_t>(wrapUInt(x));
                    const uint64_t uy = static_cast<uint64_t>(wrapUInt(y));
                    v = static_cast<int64_t>(op == "&" ? (ux & uy) : op == "|" ? (ux | uy) : (ux ^ uy));
                }
                r.i[static_cast<size_t>(k)] = a.base == Base::Int ? wrapInt(v) : wrapUInt(v);
            }
            return r;
        }
        if (op == "+" || op == "-" || op == "*" || op == "/") {
            unify(a, b, op);
            const int n = broadcastSize(a, b, op);
            Value r = a.n >= b.n ? a : b;
            for (int k = 0; k < n; ++k) {
                const size_t c = static_cast<size_t>(k);
                if (a.base == Base::Float) {
                    const float x = a.f[ci(a, k)];
                    const float y = b.f[ci(b, k)];
                    r.f[c] = op == "+" ? x + y : op == "-" ? x - y : op == "*" ? x * y : x / y;
                } else {
                    const int64_t x = a.i[ci(a, k)];
                    const int64_t y = b.i[ci(b, k)];
                    if (op == "/" && y == 0) {
                        fail("integer division by zero");
                    }
                    const int64_t v = op == "+" ? x + y : op == "-" ? x - y : op == "*" ? x * y : x / y;
                    r.i[c] = a.base == Base::Int ? wrapInt(v) : wrapUInt(v);
                }
            }
            return r;
        }
        fail("unsupported operator " + op);
    }

    static Value member(const Value& v, const std::string& name) {
        if (v.base == Base::Struct) {
            for (size_t k = 0; k < v.fieldNames.size(); ++k) {
                if (v.fieldNames[k] == name) {
                    return v.elems[k];
                }
            }
            fail("no field " + name + " in " + v.structName);
        }
        const auto idx = swizzle(v, name);
        Value r;
        r.base = v.base;
        r.n = static_cast<int>(idx.size());
        for (size_t k = 0; k < idx.size(); ++k) {
            r.f[k] = v.f[idx[k]];
            r.i[k] = v.i[idx[k]];
        }
        return r;
    }

    static std::vector<size_t> swizzle(const Value& v, const std::string& name) {
        if (!isVectorLike(v) || v.n == 1) {
            fail("swizzle ." + name + " on " + typeNameOf(v));
        }
        static const std::array<std::string_view, 3> sets{"xyzw", "rgba", "stpq"};
        for (std::string_view set : sets) {
            if (set.find(name[0]) == std::string_view::npos) {
                continue;
            }
            std::vector<size_t> idx;
            for (char c : name) {
                const size_t p = set.find(c);
                if (p == std::string_view::npos || p >= static_cast<size_t>(v.n)) {
                    fail("bad swizzle ." + name + " on " + typeNameOf(v));
                }
                idx.push_back(p);
            }
            if (idx.size() > 4) {
                fail("swizzle too long");
            }
            return idx;
        }
        fail("bad swizzle ." + name);
    }

    static size_t indexValue(const Value& idx, size_t size) {
        if (!isInteger(idx.base) || idx.n != 1) {
            fail("index must be an int or uint scalar, got " + typeNameOf(idx));
        }
        if (idx.i[0] < 0 || static_cast<size_t>(idx.i[0]) >= size) {
            fail("index " + std::to_string(idx.i[0]) + " out of range [0, " + std::to_string(size) + ")");
        }
        return static_cast<size_t>(idx.i[0]);
    }

    static Value index(const Value& v, const Value& idx) {
        if (v.base == Base::Array) {
            return v.elems[indexValue(idx, v.elems.size())];
        }
        if (isVectorLike(v) && v.n > 1) {
            const size_t k = indexValue(idx, static_cast<size_t>(v.n));
            Value r;
            r.base = v.base;
            r.f[0] = v.f[k];
            r.i[0] = v.i[k];
            return r;
        }
        fail("cannot index " + typeNameOf(v));
    }

    Value* lookup(const std::string& name) {
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            if (auto f = it->find(name); f != it->end()) {
                return &f->second;
            }
        }
        return nullptr;
    }

    Value* refOf(const Expr& e) {
        switch (e.k) {
            case Expr::K::Var: {
                if (Value* v = lookup(e.s)) {
                    return v;
                }
                if (m_globals.count(e.s)) {
                    fail("assignment to uniform " + e.s);
                }
                fail("unknown variable " + e.s);
            }
            case Expr::K::Member: {
                Value* p = refOf(*e.a[0]);
                if (p->base != Base::Struct) {
                    fail("not an lvalue");
                }
                for (size_t k = 0; k < p->fieldNames.size(); ++k) {
                    if (p->fieldNames[k] == e.s) {
                        return &p->elems[k];
                    }
                }
                fail("no field " + e.s);
            }
            case Expr::K::Index: {
                Value* p = refOf(*e.a[0]);
                if (p->base != Base::Array) {
                    fail("not an lvalue");
                }
                return &p->elems[indexValue(eval(*e.a[1]), p->elems.size())];
            }
            default: fail("expression is not an lvalue");
        }
    }

    void store(const Expr& lhs, const Value& v) {
        if (lhs.k == Expr::K::Member || lhs.k == Expr::K::Index) {
            Value* p = refOf(*lhs.a[0]);
            if (isVectorLike(*p)) {
                std::vector<size_t> idx;
                if (lhs.k == Expr::K::Member) {
                    idx = swizzle(*p, lhs.s);
                    if (std::set<size_t>(idx.begin(), idx.end()).size() != idx.size()) {
                        fail("repeated components in lvalue swizzle");
                    }
                } else {
                    if (p->n == 1) {
                        fail("cannot index a scalar");
                    }
                    idx.push_back(indexValue(eval(*lhs.a[1]), static_cast<size_t>(p->n)));
                }
                Value part;
                part.base = p->base;
                part.n = static_cast<int>(idx.size());
                const Value conv = implicitTo(v, typeNameOf(part));
                for (size_t k = 0; k < idx.size(); ++k) {
                    p->f[idx[k]] = conv.f[k];
                    p->i[idx[k]] = conv.i[k];
                }
                return;
            }
        }
        Value* dst = refOf(lhs);
        *dst = implicitTo(v, typeNameOf(*dst));
    }

    void tick() {
        if (++m_steps > 5'000'000u) {
            fail("execution budget exceeded (infinite loop?)");
        }
    }

    Value eval(const Expr& e) {
        tick();
        switch (e.k) {
            case Expr::K::Lit: return e.lit;
            case Expr::K::Var: {
                if (Value* v = lookup(e.s)) {
                    return *v;
                }
                if (auto g = m_globals.find(e.s); g != m_globals.end()) {
                    return g->second;
                }
                fail("unknown identifier " + e.s);
            }
            case Expr::K::Bin: {
                if (e.s == "&&" || e.s == "||" || e.s == "^^") {
                    const Value a = eval(*e.a[0]);
                    if (a.base != Base::Bool || a.n != 1) {
                        fail("logical operator on " + typeNameOf(a));
                    }
                    if (e.s == "&&" && a.i[0] == 0) {
                        return boolv(false);
                    }
                    if (e.s == "||" && a.i[0] != 0) {
                        return boolv(true);
                    }
                    const Value b = eval(*e.a[1]);
                    if (b.base != Base::Bool || b.n != 1) {
                        fail("logical operator on " + typeNameOf(b));
                    }
                    return boolv(e.s == "^^" ? (a.i[0] != b.i[0]) : b.i[0] != 0);
                }
                return binary(e.s, eval(*e.a[0]), eval(*e.a[1]));
            }
            case Expr::K::Un: {
                Value v = eval(*e.a[0]);
                if (e.s == "!") {
                    if (v.base != Base::Bool || v.n != 1) {
                        fail("! on " + typeNameOf(v));
                    }
                    return boolv(v.i[0] == 0);
                }
                if (e.s == "~") {
                    if (!isInteger(v.base)) {
                        fail("~ on " + typeNameOf(v));
                    }
                    for (int k = 0; k < v.n; ++k) {
                        const size_t c = static_cast<size_t>(k);
                        v.i[c] = v.base == Base::Int ? wrapInt(~v.i[c]) : wrapUInt(~v.i[c]);
                    }
                    return v;
                }
                if (!isNumeric(v.base)) {
                    fail(e.s + " on " + typeNameOf(v));
                }
                if (e.s == "-") {
                    for (int k = 0; k < v.n; ++k) {
                        const size_t c = static_cast<size_t>(k);
                        v.f[c] = -v.f[c];
                        v.i[c] = v.base == Base::Int ? wrapInt(-v.i[c]) : wrapUInt(-v.i[c]);
                    }
                }
                return v;
            }
            case Expr::K::PreInc:
            case Expr::K::PostInc: {
                const Value old = eval(*e.a[0]);
                if (!isNumeric(old.base)) {
                    fail(e.s + " on " + typeNameOf(old));
                }
                const Value one = old.base == Base::Float ? floatv(1.0f) : makeScalar(old.base, 1);
                const Value updated = binary(e.s == "++" ? "+" : "-", old, one);
                store(*e.a[0], updated);
                return e.k == Expr::K::PreInc ? updated : old;
            }
            case Expr::K::Assign: {
                Value v = eval(*e.a[1]);
                if (e.s != "=") {
                    v = binary(e.s.substr(0, e.s.size() - 1), eval(*e.a[0]), v);
                }
                store(*e.a[0], v);
                return eval(*e.a[0]);
            }
            case Expr::K::Cond: {
                const Value c = eval(*e.a[0]);
                if (c.base != Base::Bool || c.n != 1) {
                    fail("?: condition must be bool");
                }
                Value x = eval(*e.a[c.i[0] != 0 ? 1 : 2]);
                return x;
            }
            case Expr::K::Member: return member(eval(*e.a[0]), e.s);
            case Expr::K::Index: return index(eval(*e.a[0]), eval(*e.a[1]));
            case Expr::K::Call: {
                std::vector<Value> args;
                for (const ExprPtr& a : e.a) {
                    args.push_back(eval(*a));
                }
                return callFunction(e.s, std::move(args));
            }
        }
        fail("bad expression");
    }

    static Value convertComponent(Base to, Base from, float f, int64_t i) {
        Value r;
        r.base = to;
        if (to == Base::Float) {
            r.f[0] = from == Base::Float ? f : static_cast<float>(i);
        } else if (to == Base::Bool) {
            r.i[0] = from == Base::Float ? (f != 0.0f) : (i != 0);
        } else if (from == Base::Float) {
            if (!std::isfinite(f)) {
                fail("conversion of non-finite float to integer is undefined");
            }
            const double t = std::trunc(static_cast<double>(f));
            if (to == Base::Int ? (t < -2147483648.0 || t > 2147483647.0) : (t < 0.0 || t > 4294967295.0)) {
                fail("float to integer conversion out of range is undefined");
            }
            r.i[0] = static_cast<int64_t>(t);
        } else {
            r.i[0] = to == Base::Int ? wrapInt(i) : wrapUInt(i); // int <-> uint keeps the bit pattern
        }
        return r;
    }

    Value construct(Base base, int n, const std::vector<Value>& args, const std::string& name) {
        if (args.empty()) {
            fail(name + "() needs arguments");
        }
        for (const Value& a : args) {
            if (!isVectorLike(a)) {
                fail(name + "() from " + typeNameOf(a));
            }
        }
        Value r;
        r.base = base;
        r.n = n;
        if (args.size() == 1 && (args[0].n == 1 || n == 1 || args[0].n >= n)) {
            for (int k = 0; k < n; ++k) {
                const size_t src = args[0].n == 1 ? 0u : static_cast<size_t>(k);
                const Value c = convertComponent(base, args[0].base, args[0].f[src], args[0].i[src]);
                r.f[static_cast<size_t>(k)] = c.f[0];
                r.i[static_cast<size_t>(k)] = c.i[0];
            }
            return r;
        }
        int filled = 0;
        for (size_t a = 0; a < args.size(); ++a) {
            if (filled >= n) {
                fail(name + "(): too many arguments");
            }
            for (int k = 0; k < args[a].n && filled < n; ++k, ++filled) {
                const size_t src = static_cast<size_t>(k);
                const Value c = convertComponent(base, args[a].base, args[a].f[src], args[a].i[src]);
                r.f[static_cast<size_t>(filled)] = c.f[0];
                r.i[static_cast<size_t>(filled)] = c.i[0];
            }
        }
        if (filled != n) {
            fail(name + "(): not enough components");
        }
        return r;
    }

    // min/max/clamp(genType, genType|scalar...) for float, int and uint families.
    static Value minMaxClamp(const std::string& fn, std::vector<Value> args) {
        const size_t expected = fn == "clamp" ? 3u : 2u;
        if (args.size() != expected) {
            fail(fn + "() argument count");
        }
        for (Value& a : args) {
            if (!isNumeric(a.base)) {
                fail(fn + "() on " + typeNameOf(a));
            }
            requireNotNaN(a, fn.c_str());
        }
        for (size_t k = 1; k < args.size(); ++k) {
            unify(args[0], args[k], fn);
            if (args[k].n != 1 && args[k].n != args[0].n) {
                fail(fn + "(): " + typeNameOf(args[0]) + " vs " + typeNameOf(args[k]));
            }
            for (size_t j = 1; j < k; ++j) {
                unify(args[j], args[k], fn);
            }
        }
        Value r = args[0];
        for (int k = 0; k < r.n; ++k) {
            const size_t c = static_cast<size_t>(k);
            auto get = [&](const Value& v) {
                return v.base == Base::Float ? static_cast<double>(v.f[ci(v, k)]) : static_cast<double>(v.i[ci(v, k)]);
            };
            const double x = get(args[0]);
            double v = 0.0;
            if (fn == "min") {
                v = std::min(x, get(args[1]));
            } else if (fn == "max") {
                v = std::max(x, get(args[1]));
            } else {
                const double lo = get(args[1]);
                const double hi = get(args[2]);
                if (lo > hi) {
                    fail("clamp() with minVal > maxVal is undefined");
                }
                v = std::min(std::max(x, lo), hi);
            }
            if (r.base == Base::Float) {
                r.f[c] = static_cast<float>(v);
            } else {
                r.i[c] = static_cast<int64_t>(v);
            }
        }
        return r;
    }

    Value builtin(const std::string& fn, std::vector<Value>& args, bool& handled) {
        handled = true;
        if (fn == "min" || fn == "max" || fn == "clamp") {
            return minMaxClamp(fn, args);
        }
        auto requireFloat = [&](size_t count) {
            if (args.size() != count) {
                fail(fn + "() argument count");
            }
            for (Value& a : args) {
                if (isInteger(a.base)) {
                    a = toFloat(a);
                }
                if (a.base != Base::Float) {
                    fail(fn + "() on " + typeNameOf(a));
                }
            }
        };
        if (fn == "length") {
            requireFloat(1);
            float s = 0.0f;
            for (int k = 0; k < args[0].n; ++k) {
                s += args[0].f[static_cast<size_t>(k)] * args[0].f[static_cast<size_t>(k)];
            }
            return floatv(std::sqrt(s));
        }
        if (fn == "dot") {
            requireFloat(2);
            if (args[0].n != args[1].n) {
                fail("dot() size mismatch");
            }
            float s = 0.0f;
            for (int k = 0; k < args[0].n; ++k) {
                s += args[0].f[static_cast<size_t>(k)] * args[1].f[static_cast<size_t>(k)];
            }
            return floatv(s);
        }
        if (fn == "floor" || fn == "abs" || fn == "sqrt" || fn == "fract") {
            requireFloat(1);
            Value r = args[0];
            for (int k = 0; k < r.n; ++k) {
                float& x = r.f[static_cast<size_t>(k)];
                if (fn == "sqrt" && x < 0.0f) {
                    fail("sqrt() of a negative value is undefined");
                }
                x = fn == "floor" ? std::floor(x) : fn == "abs" ? std::fabs(x) : fn == "sqrt" ? std::sqrt(x) : x - std::floor(x);
            }
            return r;
        }
        handled = false;
        return {};
    }

    Value callFunction(const std::string& name, std::vector<Value> args) {
        if (auto t = basicType(name)) {
            return construct(t->first, t->second, args, name);
        }
        if (const StructDecl* sd = findStruct(name)) {
            if (args.size() != sd->members.size()) {
                fail(name + "() constructor argument count");
            }
            Value s = defaultOf(name);
            for (size_t k = 0; k < args.size(); ++k) {
                s.elems[k] = implicitTo(args[k], sd->members[k].type);
            }
            return s;
        }
        bool handled = false;
        Value b = builtin(name, args, handled);
        if (handled) {
            return b;
        }
        const Function* fn = nullptr;
        for (const auto& f : m_functions) {
            if (f->name == name) {
                if (fn != nullptr) {
                    fail("overloaded user functions are not supported: " + name);
                }
                fn = f.get();
            }
        }
        if (fn == nullptr) {
            fail("unknown function " + name);
        }
        if (args.size() != fn->params.size()) {
            fail(name + "(): argument count");
        }
        if (std::find(m_callStack.begin(), m_callStack.end(), name) != m_callStack.end()) {
            fail("recursion is not allowed in GLSL: " + name);
        }
        std::map<std::string, Value> frame;
        for (size_t k = 0; k < args.size(); ++k) {
            frame[fn->params[k].name] = implicitTo(args[k], fn->params[k].type);
        }
        // A callee never sees its caller's locals: swap in a fresh scope stack.
        std::vector<std::map<std::string, Value>> saved;
        saved.swap(m_scopes);
        m_scopes.push_back(std::move(frame));
        m_callStack.push_back(name);
        const Flow flow = exec(*fn->body);
        m_callStack.pop_back();
        m_scopes.swap(saved);
        if (fn->ret == "void") {
            if (flow == Flow::Return && m_ret.base != Base::Void) {
                fail("void function returns a value");
            }
            return {};
        }
        if (flow != Flow::Return) {
            fail("missing return in " + name);
        }
        return implicitTo(m_ret, fn->ret);
    }

    enum class Flow { Normal, Return, Break, Continue };

    Flow exec(const Stmt& s) {
        tick();
        switch (s.k) {
            case Stmt::K::Empty: return Flow::Normal;
            case Stmt::K::Block: {
                m_scopes.emplace_back();
                for (const StmtPtr& c : s.body) {
                    const Flow f = exec(*c);
                    if (f != Flow::Normal) {
                        m_scopes.pop_back();
                        return f;
                    }
                }
                m_scopes.pop_back();
                return Flow::Normal;
            }
            case Stmt::K::Decl: {
                Value v = s.e ? implicitTo(eval(*s.e), s.type) : defaultOf(s.type);
                if (m_scopes.back().count(s.name)) {
                    fail("redeclaration of " + s.name);
                }
                m_scopes.back()[s.name] = std::move(v);
                return Flow::Normal;
            }
            case Stmt::K::Expr: (void)eval(*s.e); return Flow::Normal;
            case Stmt::K::If: {
                if (evalCondition(*s.e)) {
                    return exec(*s.then);
                }
                return s.els ? exec(*s.els) : Flow::Normal;
            }
            case Stmt::K::For:
            case Stmt::K::While: {
                m_scopes.emplace_back();
                if (s.init) {
                    (void)exec(*s.init);
                }
                for (;;) {
                    if (s.e && !evalCondition(*s.e)) {
                        break;
                    }
                    const Flow f = exec(*s.then);
                    if (f == Flow::Return) {
                        m_scopes.pop_back();
                        return f;
                    }
                    if (f == Flow::Break) {
                        break;
                    }
                    if (s.step) {
                        (void)eval(*s.step);
                    }
                }
                m_scopes.pop_back();
                return Flow::Normal;
            }
            case Stmt::K::Return: m_ret = s.e ? eval(*s.e) : Value{}; return Flow::Return;
            case Stmt::K::Break: return Flow::Break;
            case Stmt::K::Continue: return Flow::Continue;
        }
        fail("bad statement");
    }

    bool evalCondition(const Expr& e) {
        const Value c = eval(e);
        if (c.base != Base::Bool || c.n != 1) {
            fail("condition must be a bool scalar, got " + typeNameOf(c));
        }
        return c.i[0] != 0;
    }

    std::vector<Token> m_tok;
    size_t m_pos{0};
    std::vector<StructDecl> m_structs;
    std::vector<BlockDecl> m_blocks;
    std::vector<std::unique_ptr<Function>> m_functions;
    std::map<std::string, Value> m_globals;
    std::vector<std::map<std::string, Value>> m_scopes;
    std::vector<std::string> m_callStack;
    Value m_ret;
    size_t m_steps{0};
};

} // namespace dynlight_test::glsl
