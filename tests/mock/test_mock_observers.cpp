#include "audio/AudioEngine.hpp"
#include "physics/Player.hpp"
#include "ui/CraftingSystem.hpp"
#include "world/Block.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace betacraft;
using namespace testing;

// 1. Mock listener for World modifications
class IWorldEventListener {
public:
    virtual ~IWorldEventListener() = default;
    virtual void onBlockPlaced(const core::BlockPos& pos, world::BlockId block) = 0;
    virtual void onBlockDestroyed(const core::BlockPos& pos, world::BlockId block) = 0;
    virtual void onPistonActivated(const core::BlockPos& pos, core::FaceDir dir) = 0;
};

class MockWorldEventListener : public IWorldEventListener {
public:
    MOCK_METHOD(void, onBlockPlaced, (const core::BlockPos& pos, world::BlockId block), (override));
    MOCK_METHOD(void, onBlockDestroyed, (const core::BlockPos& pos, world::BlockId block), (override));
    MOCK_METHOD(void, onPistonActivated, (const core::BlockPos& pos, core::FaceDir dir), (override));
};

// 2. Mock observer for Player Inventory events
class IInventoryObserver {
public:
    virtual ~IInventoryObserver() = default;
    virtual void onSlotUpdated(size_t slotIndex, world::BlockId id, int32_t count) = 0;
    virtual void onItemCrafted(world::BlockId resultId, int32_t count) = 0;
};

class MockInventoryObserver : public IInventoryObserver {
public:
    MOCK_METHOD(void, onSlotUpdated, (size_t slotIndex, world::BlockId id, int32_t count), (override));
    MOCK_METHOD(void, onItemCrafted, (world::BlockId resultId, int32_t count), (override));
};

// 3. Mock Sound Dispatcher
class ISoundDispatcher {
public:
    virtual ~ISoundDispatcher() = default;
    virtual void playSound(audio::SoundEffect effect, float volume, float pitch) = 0;
};

class MockSoundDispatcher : public ISoundDispatcher {
public:
    MOCK_METHOD(void, playSound, (audio::SoundEffect effect, float volume, float pitch), (override));
};

TEST(GMockTests, WorldModificationEventSequence) {
    MockWorldEventListener listener;
    const core::BlockPos pos1{10, 64, 10};
    const core::BlockPos pos2{10, 64, 11};

    {
        InSequence seq;
        EXPECT_CALL(listener, onBlockPlaced(pos1, world::BlockId::Stone)).Times(1);
        EXPECT_CALL(listener, onBlockPlaced(pos2, world::BlockId::Piston)).Times(1);
        EXPECT_CALL(listener, onPistonActivated(pos2, core::FaceDir::PosX)).Times(1);
        EXPECT_CALL(listener, onBlockDestroyed(pos1, world::BlockId::Stone)).Times(1);
    }

    listener.onBlockPlaced(pos1, world::BlockId::Stone);
    listener.onBlockPlaced(pos2, world::BlockId::Piston);
    listener.onPistonActivated(pos2, core::FaceDir::PosX);
    listener.onBlockDestroyed(pos1, world::BlockId::Stone);
}

TEST(GMockTests, InventoryObserverWithMatchers) {
    MockInventoryObserver observer;

    EXPECT_CALL(observer, onSlotUpdated(
        0,
        Eq(world::BlockId::OakLog),
        Gt(0)
    )).Times(1);

    EXPECT_CALL(observer, onItemCrafted(
        Eq(world::BlockId::OakPlanks),
        Eq(4)
    )).Times(1);

    observer.onSlotUpdated(0, world::BlockId::OakLog, 16);
    observer.onItemCrafted(world::BlockId::OakPlanks, 4);
}

TEST(GMockTests, SoundDispatcherMocking) {
    MockSoundDispatcher soundMock;

    EXPECT_CALL(soundMock, playSound(
        Eq(audio::SoundEffect::StepGrass),
        FloatNear(0.22f, 0.05f),
        AllOf(Ge(0.85f), Le(1.15f))
    )).Times(2);

    soundMock.playSound(audio::SoundEffect::StepGrass, 0.22f, 1.0f);
    soundMock.playSound(audio::SoundEffect::StepGrass, 0.20f, 0.95f);
}
