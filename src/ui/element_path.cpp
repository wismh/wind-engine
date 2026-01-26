#include "element_path.h"

namespace engine::ui {
    namespace {

        const Element *step_const(const Element &current, std::size_t step) {
            if ((step & kGeneratedPathBit) != 0) {
                const std::size_t index = step & ~kGeneratedPathBit;
                if (index >= current.generated_items.size()) {
                    return nullptr;
                }
                return &current.generated_items[index];
            }
            if (step >= current.children.size()) {
                return nullptr;
            }
            return &current.children[step];
        }

        bool build_element_path(const Element &current, const Element *target, std::vector<std::size_t> &path) {
            if (&current == target) {
                return true;
            }
            for (std::size_t i = 0; i < current.children.size(); ++i) {
                path.push_back(i);
                if (build_element_path(current.children[i], target, path)) {
                    return true;
                }
                path.pop_back();
            }
            for (std::size_t i = 0; i < current.generated_items.size(); ++i) {
                path.push_back(i | kGeneratedPathBit);
                if (build_element_path(current.generated_items[i], target, path)) {
                    return true;
                }
                path.pop_back();
            }
            return false;
        }

    } // namespace

    std::vector<std::size_t> find_element_path(const Element &root, const Element *target) {
        std::vector<std::size_t> path;
        if (target == nullptr) {
            return path;
        }
        build_element_path(root, target, path);
        return path;
    }

    Element *resolve_element_path(Element &root, const std::vector<std::size_t> &path) {
        Element *current = &root;
        for (const std::size_t step: path) {
            if ((step & kGeneratedPathBit) != 0) {
                const std::size_t index = step & ~kGeneratedPathBit;
                if (index >= current->generated_items.size()) {
                    return nullptr;
                }
                current = &current->generated_items[index];
            } else {
                if (step >= current->children.size()) {
                    return nullptr;
                }
                current = &current->children[step];
            }
        }
        return current;
    }

    const void *path_generated_owner(const Element &root, const std::vector<std::size_t> &path) {
        const Element *current = &root;
        const void *owner = nullptr;
        for (const std::size_t step: path) {
            current = step_const(*current, step);
            if (current == nullptr) {
                return owner;
            }
            if (current->generated_owner != nullptr) {
                owner = current->generated_owner;
            }
        }
        return owner;
    }

    Element *resolve_inspector_element(Element &root, const std::vector<std::size_t> &path, const void *owner) {
        Element *current = &root;
        for (const std::size_t step: path) {
            if ((step & kGeneratedPathBit) != 0) {
                const std::size_t index = step & ~kGeneratedPathBit;
                Element *next = nullptr;
                if (owner != nullptr) {
                    if (index < current->generated_items.size() &&
                        current->generated_items[index].generated_owner == owner) {
                        next = &current->generated_items[index];
                    } else {
                        for (Element &item: current->generated_items) {
                            if (item.generated_owner == owner) {
                                next = &item;
                                break;
                            }
                        }
                    }
                }
                if (next == nullptr) {
                    if (index >= current->generated_items.size()) {
                        return nullptr;
                    }
                    next = &current->generated_items[index];
                }
                current = next;
            } else {
                if (step >= current->children.size()) {
                    return nullptr;
                }
                current = &current->children[step];
            }
        }
        return current;
    }

} // namespace engine::ui
