#include "internal.hpp"

static
SceneTree* get_root(SceneNode* node)
{
    auto* root = dynamic_cast<SceneTree*>(node) ?: node->parent;
    if (!root) return nullptr;

    while (root->parent) {
        root = root->parent;
    }

    return root;
}

void scene_node_damage(SceneNode* node)
{
    auto* root = get_root(node);

    if (!root || !root->scene) {
        return;
    }

    node->damage(root->scene);
}

static
void dispatch_damage(Scene* scene)
{
    defer { scene->damage.queued = {}; };

    if (scene->damage.queued.contains(SceneDamageType::input)) {
        scene_update_pointers(scene);
    }

    if (scene->damage.queued.contains(SceneDamageType::visual)) {
        for (auto* output : scene->outputs) {
            scene_output_request_frame(output);
        }
    }
}

void scene_enqueue_damage(Scene* scene, SceneDamageType type)
{
    auto enqueue = scene->damage.queued.empty();
    scene->damage.queued |= type;

    if (!enqueue) return;

    exec_enqueue(scene->exec, [scene = Weak(scene)] {
        if (scene) dispatch_damage(scene.get());
    });
}