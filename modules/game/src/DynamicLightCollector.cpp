#include "game/DynamicLightCollector.hpp"

#include "crafting/ItemIds.hpp"
#include "ecs/Components.hpp"
#include "ecs/Systems.hpp"

namespace betacraft::game {

void collectDynamicLights(lighting::DynamicLightManager& manager, const physics::Player& player,
                          ecs::Registry* entities, std::optional<float> entityAlpha) {
    manager.clear();

    // Only blocks emit light: no item at 256 and above glows in Beta (not even glowstone dust).
    const physics::InventorySlot held = player.getHeldItem();
    if (const auto block = crafting::toBlockId(held.id); !held.isEmpty() && block) {
        (void)manager.submitHeldItem(player.getEyePosition(), player.getLookDirection(), *block);
    }

    if (entities == nullptr) {
        return;
    }
    const float alpha = entityAlpha.value_or(player.getRenderAlpha());
    // Item entities are anchored at their feet; light from the middle of the item instead.
    const glm::vec3 centre(0.0f, ecs::EntityFactory::ITEM_SIZE * 0.5f, 0.0f);
    entities->view<ecs::TransformComp, ecs::ItemDropComp>().each(
        [&](const ecs::TransformComp& t, const ecs::ItemDropComp& item) {
            const auto block = crafting::toBlockId(item.id);
            if (item.count <= 0 || !block) {
                return;
            }
            (void)manager.submitDroppedItem(glm::mix(t.prevPos, t.pos, alpha) + centre, *block);
        });
}

} // namespace betacraft::game
