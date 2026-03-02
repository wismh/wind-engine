#include <gtest/gtest.h>

#include "render/context_objects.h"

#include <vector>

using engine::render::ContextObjects;

namespace {

int primary_handle = 0;
int secondary_handle = 0;

}

TEST(ContextObjects, EachContextGetsItsOwnKey) {
    ContextObjects contexts;
    const ContextObjects::Key primary = contexts.add_context(&primary_handle);
    const ContextObjects::Key secondary = contexts.add_context(&secondary_handle);
    EXPECT_NE(primary, 0u);
    EXPECT_NE(secondary, 0u);
    EXPECT_NE(primary, secondary);
    EXPECT_EQ(contexts.key(&primary_handle), primary);
    EXPECT_EQ(contexts.key(&secondary_handle), secondary);
    EXPECT_TRUE(contexts.is_live(primary));
    EXPECT_TRUE(contexts.is_live(secondary));
}

TEST(ContextObjects, NoContextAndAnUnknownOneHaveNoKey) {
    ContextObjects contexts;
    EXPECT_EQ(contexts.add_context(nullptr), 0u);
    EXPECT_EQ(contexts.key(nullptr), 0u);
    EXPECT_EQ(contexts.key(&primary_handle), 0u);
    EXPECT_FALSE(contexts.is_live(0));
}

TEST(ContextObjects, AReusedHandleGetsANewKey) {
    // A driver may hand out the handle of a destroyed context again. A VAO cached under the old key must not be
    // bound in the new context.
    ContextObjects contexts;
    const ContextObjects::Key old_key = contexts.add_context(&secondary_handle);
    contexts.remove_context(&secondary_handle);
    EXPECT_FALSE(contexts.is_live(old_key));
    EXPECT_EQ(contexts.key(&secondary_handle), 0u);

    const ContextObjects::Key new_key = contexts.add_context(&secondary_handle);
    EXPECT_NE(new_key, old_key);
    EXPECT_TRUE(contexts.is_live(new_key));
    EXPECT_FALSE(contexts.is_live(old_key));
}

TEST(ContextObjects, DeferredDeletesWaitForTheirOwnContext) {
    ContextObjects contexts;
    const ContextObjects::Key primary = contexts.add_context(&primary_handle);
    const ContextObjects::Key secondary = contexts.add_context(&secondary_handle);
    contexts.defer_delete(secondary, 7);
    contexts.defer_delete(secondary, 9);
    contexts.defer_delete(secondary, 0);

    EXPECT_TRUE(contexts.take_deferred(primary).empty());
    EXPECT_EQ(contexts.take_deferred(secondary), (std::vector<unsigned int>{7, 9}));
    EXPECT_TRUE(contexts.take_deferred(secondary).empty());
}

TEST(ContextObjects, DestroyingAContextDropsItsDeferredDeletes) {
    ContextObjects contexts;
    const ContextObjects::Key secondary = contexts.add_context(&secondary_handle);
    contexts.defer_delete(secondary, 7);
    contexts.remove_context(&secondary_handle);
    contexts.defer_delete(secondary, 9);

    const ContextObjects::Key reopened = contexts.add_context(&secondary_handle);
    EXPECT_TRUE(contexts.take_deferred(reopened).empty());
    EXPECT_TRUE(contexts.take_deferred(secondary).empty());
}
