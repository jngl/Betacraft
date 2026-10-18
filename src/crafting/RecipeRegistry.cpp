#include "crafting/RecipeRegistry.hpp"

namespace betacraft::crafting {

namespace {

const std::vector<uint32_t> kNoCandidates{};

// Applies one craft's worth of an ingredient to its grid slot.
void useSlot(CraftingGrid& grid, int x, int y, const Ingredient& ing, CraftOutcome& out) noexcept {
    SlotStack s = grid.get(x, y);
    const uint16_t maxDamage = maxDurability(s.id);
    if (ing.damagePerCraft > 0 && maxDamage > 0) {
        // Beta 1.7.3 ItemStack.damageItem semantics, identical to
        // gameplay::ItemStack::applyWear: the tool breaks only once damage
        // EXCEEDS maxDamage (maxDamage + 1 uses), and breaking removes one
        // item of the stack with the next one starting fresh.
        const uint32_t damage = static_cast<uint32_t>(s.damage) + ing.damagePerCraft;
        if (damage > maxDamage) {
            --s.count;
            s.damage = 0;
        } else {
            s.damage = static_cast<uint16_t>(damage);
        }
        (void)grid.set(x, y, s);
        return;
    }
    --s.count;
    if (s.count == 0) {
        s = SlotStack{};
    }
    if (ing.remainder != kAir) {
        if (s.empty()) {
            s = SlotStack{ing.remainder, 1, 0};
        } else if (out.byproductCount < out.byproducts.size()) {
            out.byproducts[out.byproductCount++] = SlotStack{ing.remainder, 1, 0};
        }
    }
    (void)grid.set(x, y, s);
}

} // namespace

RecipeError RecipeRegistry::addShaped(std::span<const std::string_view> rows, std::span<const PatternKey> keys,
                                      RecipeResult result, bool mirrorable, std::string_view label) {
    ShapedParse parsed = ShapedRecipe::parse(rows, keys, result, mirrorable);
    if (!parsed.recipe) {
        return parsed.error;
    }
    const ShapedRecipe& r = *parsed.recipe;
    const auto index = static_cast<uint32_t>(m_entries.size());
    const uint32_t key = shapeKey(r.width(), r.height(), r.occupancyMask());
    m_shapedIndex[key].push_back(index);
    if (r.mirrorable()) {
        const uint32_t mirroredKey = shapeKey(r.width(), r.height(), r.mirroredOccupancyMask());
        if (mirroredKey != key) {
            m_shapedIndex[mirroredKey].push_back(index);
        }
    }
    m_entries.push_back(Entry{RecipeKind::Shaped, static_cast<uint32_t>(m_shaped.size()), std::string{label}});
    m_shaped.push_back(r);
    return RecipeError::None;
}

RecipeError RecipeRegistry::addShaped(std::initializer_list<std::string_view> rows,
                                      std::initializer_list<PatternKey> keys, RecipeResult result,
                                      bool mirrorable, std::string_view label) {
    return addShaped(std::span<const std::string_view>{rows.begin(), rows.size()},
                     std::span<const PatternKey>{keys.begin(), keys.size()}, result, mirrorable, label);
}

RecipeError RecipeRegistry::addShapeless(std::span<const Ingredient> ingredients, RecipeResult result,
                                         std::string_view label) {
    ShapelessParse parsed = ShapelessRecipe::create(ingredients, result);
    if (!parsed.recipe) {
        return parsed.error;
    }
    const auto index = static_cast<uint32_t>(m_entries.size());
    m_shapelessIndex[parsed.recipe->ingredientCount()].push_back(index);
    m_entries.push_back(
        Entry{RecipeKind::Shapeless, static_cast<uint32_t>(m_shapeless.size()), std::string{label}});
    m_shapeless.push_back(*parsed.recipe);
    return RecipeError::None;
}

RecipeError RecipeRegistry::addShapeless(std::initializer_list<Ingredient> ingredients, RecipeResult result,
                                         std::string_view label) {
    return addShapeless(std::span<const Ingredient>{ingredients.begin(), ingredients.size()}, result, label);
}

std::string_view RecipeRegistry::label(size_t index) const noexcept {
    return index < m_entries.size() ? std::string_view{m_entries[index].label} : std::string_view{};
}

RecipeKind RecipeRegistry::kind(size_t index) const noexcept {
    return index < m_entries.size() ? m_entries[index].kind : RecipeKind::Shaped;
}

RecipeResult RecipeRegistry::result(size_t index) const noexcept {
    if (const ShapedRecipe* r = shaped(index)) {
        return r->result();
    }
    if (const ShapelessRecipe* r = shapeless(index)) {
        return r->result();
    }
    return {};
}

const ShapedRecipe* RecipeRegistry::shaped(size_t index) const noexcept {
    if (index >= m_entries.size() || m_entries[index].kind != RecipeKind::Shaped) {
        return nullptr;
    }
    return &m_shaped[m_entries[index].slot];
}

const ShapelessRecipe* RecipeRegistry::shapeless(size_t index) const noexcept {
    if (index >= m_entries.size() || m_entries[index].kind != RecipeKind::Shapeless) {
        return nullptr;
    }
    return &m_shapeless[m_entries[index].slot];
}

RecipeRegistry::Candidates RecipeRegistry::candidatesFor(const CraftingGrid& grid) const noexcept {
    Candidates c{&kNoCandidates, &kNoCandidates};
    const auto box = grid.bounds();
    if (!box) {
        return c;
    }
    uint32_t mask = 0;
    for (int y = 0; y < box->height(); ++y) {
        for (int x = 0; x < box->width(); ++x) {
            if (!grid.get(box->minX + x, box->minY + y).empty()) {
                mask |= 1u << (y * box->width() + x);
            }
        }
    }
    if (const auto it = m_shapedIndex.find(shapeKey(box->width(), box->height(), mask));
        it != m_shapedIndex.end()) {
        c.shaped = &it->second;
    }
    c.shapeless = &m_shapelessIndex[grid.occupiedCount()];
    return c;
}

size_t RecipeRegistry::candidateCount(const CraftingGrid& grid) const noexcept {
    const Candidates c = candidatesFor(grid);
    return c.shaped->size() + c.shapeless->size();
}

std::optional<RecipeRegistry::Found> RecipeRegistry::find(const CraftingGrid& grid) const noexcept {
    const Candidates c = candidatesFor(grid);
    // Both candidate lists are sorted by registration index (append-only), so a
    // merge walk visits them in global priority order.
    size_t i = 0, j = 0;
    while (i < c.shaped->size() || j < c.shapeless->size()) {
        const bool takeShaped =
            j >= c.shapeless->size() || (i < c.shaped->size() && (*c.shaped)[i] < (*c.shapeless)[j]);
        const uint32_t index = takeShaped ? (*c.shaped)[i++] : (*c.shapeless)[j++];
        const Entry& e = m_entries[index];
        Found f{};
        f.index = index;
        if (e.kind == RecipeKind::Shaped) {
            const ShapedRecipe& r = m_shaped[e.slot];
            if (!r.fitsIn(grid.size())) {
                continue;
            }
            if (const auto m = r.match(grid)) {
                f.shaped = *m;
                return f;
            }
        } else {
            const ShapelessRecipe& r = m_shapeless[e.slot];
            if (!r.fitsIn(grid.size())) {
                continue;
            }
            if (const auto m = r.match(grid)) {
                f.shapeless = *m;
                return f;
            }
        }
    }
    return std::nullopt;
}

std::optional<CraftMatch> RecipeRegistry::match(const CraftingGrid& grid) const noexcept {
    const auto f = find(grid);
    if (!f) {
        return std::nullopt;
    }
    const RecipeKind k = m_entries[f->index].kind;
    return CraftMatch{f->index, k, result(f->index), k == RecipeKind::Shaped && f->shaped.mirrored};
}

std::optional<CraftOutcome> RecipeRegistry::craft(CraftingGrid& grid) const noexcept {
    const auto f = find(grid);
    if (!f) {
        return std::nullopt;
    }
    CraftOutcome out{};
    out.recipeIndex = f->index;
    out.result = result(f->index);
    if (const ShapedRecipe* r = shaped(f->index)) {
        const ShapedMatch& m = f->shaped;
        for (int y = 0; y < r->height(); ++y) {
            for (int x = 0; x < r->width(); ++x) {
                const Ingredient* ing = r->cell(m.mirrored ? r->width() - 1 - x : x, y);
                if (ing != nullptr) {
                    useSlot(grid, m.originX + x, m.originY + y, *ing, out);
                }
            }
        }
    } else if (const ShapelessRecipe* r = shapeless(f->index)) {
        for (size_t i = 0; i < grid.slotCount(); ++i) {
            const int8_t ing = f->shapeless.slotToIngredient[i];
            if (ing >= 0) {
                const int w = grid.width();
                useSlot(grid, static_cast<int>(i) % w, static_cast<int>(i) / w,
                        r->ingredient(static_cast<size_t>(ing)), out);
            }
        }
    }
    return out;
}

} // namespace betacraft::crafting
