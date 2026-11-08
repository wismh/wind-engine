#include "broadphase.h"

#include <algorithm>
#include <cmath>

namespace engine {
namespace {

[[nodiscard]] Aabb combine_aabb(const Aabb& a, const Aabb& b) {
    Aabb out;
    out.min.x = std::min(a.min.x, b.min.x);
    out.min.y = std::min(a.min.y, b.min.y);
    out.min.z = std::min(a.min.z, b.min.z);
    out.max.x = std::max(a.max.x, b.max.x);
    out.max.y = std::max(a.max.y, b.max.y);
    out.max.z = std::max(a.max.z, b.max.z);
    return out;
}

[[nodiscard]] float surface_area(const Aabb& box) {
    const glm::vec3 size = box.max - box.min;
    return 2.f * (size.x * size.y + size.y * size.z + size.z * size.x);
}

}

int Broadphase::allocate_node() {
    if (free_ >= 0) {
        const int id = free_;
        free_ = nodes_[static_cast<std::size_t>(id)].next;
        nodes_[static_cast<std::size_t>(id)] = Node{};
        return id;
    }
    nodes_.push_back(Node{});
    return static_cast<int>(nodes_.size()) - 1;
}

float Broadphase::leaf_cost(const Aabb& leaf_box, int child, float inheritance) const {
    const Node& node = nodes_[static_cast<std::size_t>(child)];
    const float grown = surface_area(combine_aabb(leaf_box, node.box));
    if (node.child1 < 0) {
        return grown + inheritance;
    }
    return grown - surface_area(node.box) + inheritance;
}

void Broadphase::insert_leaf(int leaf) {
    if (root_ < 0) {
        root_ = leaf;
        nodes_[static_cast<std::size_t>(leaf)].parent = -1;
        return;
    }

    const Aabb leaf_box = nodes_[static_cast<std::size_t>(leaf)].box;
    int index = root_;
    while (nodes_[static_cast<std::size_t>(index)].child1 >= 0) {
        const int child1 = nodes_[static_cast<std::size_t>(index)].child1;
        const int child2 = nodes_[static_cast<std::size_t>(index)].child2;
        const float area_here = surface_area(nodes_[static_cast<std::size_t>(index)].box);
        const float combined_area = surface_area(combine_aabb(nodes_[static_cast<std::size_t>(index)].box, leaf_box));
        const float cost = 2.f * combined_area;
        const float inheritance = 2.f * (combined_area - area_here);
        const float cost1 = leaf_cost(leaf_box, child1, inheritance);
        const float cost2 = leaf_cost(leaf_box, child2, inheritance);
        if (cost < cost1 && cost < cost2) {
            break;
        }
        // Equal cost keeps child1, the lower child slot.
        index = cost2 < cost1 ? child2 : child1;
    }

    const int sibling = index;
    const int old_parent = nodes_[static_cast<std::size_t>(sibling)].parent;
    const int parent = allocate_node();
    nodes_[static_cast<std::size_t>(parent)].parent = old_parent;
    const Aabb parent_box = combine_aabb(leaf_box, nodes_[static_cast<std::size_t>(sibling)].box);
    nodes_[static_cast<std::size_t>(parent)].box = parent_box;
    nodes_[static_cast<std::size_t>(parent)].height = nodes_[static_cast<std::size_t>(sibling)].height + 1;
    nodes_[static_cast<std::size_t>(parent)].child1 = sibling;
    nodes_[static_cast<std::size_t>(parent)].child2 = leaf;
    nodes_[static_cast<std::size_t>(sibling)].parent = parent;
    nodes_[static_cast<std::size_t>(leaf)].parent = parent;
    if (old_parent >= 0) {
        Node& previous = nodes_[static_cast<std::size_t>(old_parent)];
        if (previous.child1 == sibling) {
            previous.child1 = parent;
        } else {
            previous.child2 = parent;
        }
    } else {
        root_ = parent;
    }

    int walk = nodes_[static_cast<std::size_t>(leaf)].parent;
    while (walk >= 0) {
        walk = balance(walk);
        Node& node = nodes_[static_cast<std::size_t>(walk)];
        const Node& child1 = nodes_[static_cast<std::size_t>(node.child1)];
        const Node& child2 = nodes_[static_cast<std::size_t>(node.child2)];
        node.height = 1 + std::max(child1.height, child2.height);
        node.box = combine_aabb(child1.box, child2.box);
        walk = node.parent;
    }
}

int Broadphase::balance(int i_a) {
    Node& a = nodes_[static_cast<std::size_t>(i_a)];
    if (a.child1 < 0 || a.height < 2) {
        return i_a;
    }

    const int i_b = a.child1;
    const int i_c = a.child2;
    Node& b = nodes_[static_cast<std::size_t>(i_b)];
    Node& c = nodes_[static_cast<std::size_t>(i_c)];

    const int balance_factor = c.height - b.height;

    if (balance_factor > 1) {
        const int i_f = c.child1;
        const int i_g = c.child2;
        Node& f = nodes_[static_cast<std::size_t>(i_f)];
        Node& g = nodes_[static_cast<std::size_t>(i_g)];

        c.child1 = i_a;
        c.parent = a.parent;
        a.parent = i_c;

        if (c.parent >= 0) {
            Node& c_parent = nodes_[static_cast<std::size_t>(c.parent)];
            if (c_parent.child1 == i_a) {
                c_parent.child1 = i_c;
            } else {
                c_parent.child2 = i_c;
            }
        } else {
            root_ = i_c;
        }

        if (f.height > g.height) {
            c.child2 = i_f;
            a.child2 = i_g;
            g.parent = i_a;
            a.box = combine_aabb(b.box, g.box);
            c.box = combine_aabb(a.box, f.box);
            a.height = 1 + std::max(b.height, g.height);
            c.height = 1 + std::max(a.height, f.height);
        } else {
            c.child2 = i_g;
            a.child2 = i_f;
            f.parent = i_a;
            a.box = combine_aabb(b.box, f.box);
            c.box = combine_aabb(a.box, g.box);
            a.height = 1 + std::max(b.height, f.height);
            c.height = 1 + std::max(a.height, g.height);
        }

        return i_c;
    }

    if (balance_factor < -1) {
        const int i_d = b.child1;
        const int i_e = b.child2;
        Node& d = nodes_[static_cast<std::size_t>(i_d)];
        Node& e = nodes_[static_cast<std::size_t>(i_e)];

        b.child1 = i_a;
        b.parent = a.parent;
        a.parent = i_b;

        if (b.parent >= 0) {
            Node& b_parent = nodes_[static_cast<std::size_t>(b.parent)];
            if (b_parent.child1 == i_a) {
                b_parent.child1 = i_b;
            } else {
                b_parent.child2 = i_b;
            }
        } else {
            root_ = i_b;
        }

        if (d.height > e.height) {
            b.child2 = i_d;
            a.child1 = i_e;
            e.parent = i_a;
            a.box = combine_aabb(c.box, e.box);
            b.box = combine_aabb(a.box, d.box);
            a.height = 1 + std::max(c.height, e.height);
            b.height = 1 + std::max(a.height, d.height);
        } else {
            b.child2 = i_e;
            a.child1 = i_d;
            d.parent = i_a;
            a.box = combine_aabb(c.box, d.box);
            b.box = combine_aabb(a.box, e.box);
            a.height = 1 + std::max(c.height, d.height);
            b.height = 1 + std::max(a.height, e.height);
        }

        return i_b;
    }

    return i_a;
}

void Broadphase::rebuild(std::span<const Aabb> boxes) {
    nodes_.clear();
    nodes_.reserve(boxes.size() * 2);
    leaf_of_.assign(boxes.size(), -1);
    root_ = -1;
    free_ = -1;
    for (int i = 0; i < static_cast<int>(boxes.size()); ++i) {
        if (!aabb_valid(boxes[static_cast<std::size_t>(i)])) {
            continue;
        }
        const int leaf = allocate_node();
        nodes_[static_cast<std::size_t>(leaf)].box = boxes[static_cast<std::size_t>(i)];
        nodes_[static_cast<std::size_t>(leaf)].leaf = i;
        leaf_of_[static_cast<std::size_t>(i)] = leaf;
        insert_leaf(leaf);
    }
}

void Broadphase::query_pairs(std::vector<std::pair<int, int>>& out) const {
    out.clear();
    if (root_ < 0) {
        return;
    }
    std::vector<int> stack;
    stack.reserve(32);
    for (int i = 0; i < static_cast<int>(leaf_of_.size()); ++i) {
        const int leaf = leaf_of_[static_cast<std::size_t>(i)];
        if (leaf < 0) {
            continue;
        }
        const Aabb box = nodes_[static_cast<std::size_t>(leaf)].box;
        stack.clear();
        stack.push_back(root_);
        while (!stack.empty()) {
            const int node_index = stack.back();
            stack.pop_back();
            const Node& node = nodes_[static_cast<std::size_t>(node_index)];
            if (!aabb_overlaps(box, node.box)) {
                continue;
            }
            if (node.child1 < 0) {
                if (node.leaf > i) {
                    out.emplace_back(i, node.leaf);
                }
                continue;
            }
            // child1 is pushed last so it is visited first.
            stack.push_back(node.child2);
            stack.push_back(node.child1);
        }
    }
}

void Broadphase::query(const Aabb& box, std::vector<int>& out) const {
    out.clear();
    query(box, [&](int leaf) {
        out.push_back(leaf);
    });
}

std::vector<std::pair<int, int>> query_sorted_pairs(std::span<const Aabb> boxes) {
    Broadphase tree;
    tree.rebuild(boxes);
    std::vector<std::pair<int, int>> pairs;
    tree.query_pairs(pairs);
    std::sort(pairs.begin(), pairs.end());
    return pairs;
}

}
