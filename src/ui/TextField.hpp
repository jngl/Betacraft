#pragma once

#include "ui/BitmapFont.hpp"
#include "ui/TitleScreen.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace betacraft::ui {

// Beta ChatAllowedCharacters.allowedCharactersArray (despite its name, the characters a world
// folder name may not contain): '/', '\n', '\r', '\t', '\0', '\f', '`', '?', '*', '\\', '<',
// '>', '|', '"', ':'. The control characters are refused by every TextField anyway.
inline constexpr std::string_view kForbiddenFileNameChars{"/`?*\\<>|\":"};

// S7.8 one-line text input (Beta GuiTextField): SDL_TEXTINPUT text goes through insertText,
// Backspace through backspace(). Only printable ASCII is accepted (the bitmap font has no other
// glyph, and a UTF-8 sequence is dropped whole), minus the field's `forbidden` characters, up to
// `maxLength` characters.
class TextField {
public:
    static constexpr size_t kDefaultMaxLength = 32;

    explicit TextField(size_t maxLength = kDefaultMaxLength, std::string_view forbidden = {});

    // True for printable ASCII not in `forbidden`.
    [[nodiscard]] static bool isAllowedChar(char c, std::string_view forbidden) noexcept;

    // Appends the allowed characters of `utf8` that still fit; returns how many were added.
    size_t insertText(std::string_view utf8);
    // Removes the last character; false when the field is empty.
    bool backspace() noexcept;
    // Replaces the text, filtered and cut like typed text.
    void setText(std::string_view text);
    void clear() noexcept { m_text.clear(); }

    [[nodiscard]] const std::string& text() const noexcept { return m_text; }
    [[nodiscard]] size_t maxLength() const noexcept { return m_maxLength; }
    void setFocused(bool focused) noexcept { m_focused = focused; }
    [[nodiscard]] bool isFocused() const noexcept { return m_focused; }

    // Black box with a grey border (white when focused) and the text; the caret '_' blinks
    // after it while focused (`caretVisible`).
    void render(std::vector<UiQuadVertex>& out, float x, float y, float width, float height, bool caretVisible) const;

private:
    std::string m_text;
    std::string m_forbidden;
    size_t m_maxLength;
    bool m_focused{false};
};

// Shared drawing of the S7.8 menu screens.
void appendSolidQuad(std::vector<UiQuadVertex>& out, float x0, float y0, float x1, float y1, const glm::vec4& color);
// A title-screen style button; a disabled one is darker and never highlighted.
void appendMenuButton(std::vector<UiQuadVertex>& out, const MenuButton& button, std::string_view label, bool hovered,
                      bool enabled = true);
[[nodiscard]] bool isInside(const MenuButton& button, float x, float y) noexcept;

} // namespace betacraft::ui
