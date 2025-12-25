#include "util.hpp"

wrei_region::wrei_region()
{
    pixman_region32_init(&region);
}

wrei_region::wrei_region(rect2i32 rect)
{
    pixman_region32_init_rect(&region, rect.origin.x, rect.origin.y, rect.extent.x, rect.extent.y);
}


wrei_region::wrei_region(const wrei_region& other)
{
    pixman_region32_init(&region);
    pixman_region32_copy(&region, &other.region);
}

wrei_region& wrei_region::operator=(const wrei_region& other)
{
    if (this != &other) {
        pixman_region32_clear(&region);
        pixman_region32_copy(&region, &other.region);
    }
    return *this;
}

wrei_region::wrei_region(wrei_region&& other)
{
    region = other.region;
    other.region = {};
    pixman_region32_init(&other.region);
}

wrei_region& wrei_region::operator=(wrei_region&& other)
{
    if (this != &other) {
        pixman_region32_fini(&region);
        region = other.region;
        other.region = {};
        pixman_region32_init(&other.region);
    }
    return *this;
}

wrei_region::~wrei_region()
{
    pixman_region32_fini(&region);
}

void wrei_region::clear()
{
    pixman_region32_clear(&region);
}

bool wrei_region::empty() const
{
    return pixman_region32_empty(&region);
}

void wrei_region::add(rect2i32 rect)
{
    pixman_region32_union_rect(&region, &region, rect.origin.x, rect.origin.y, rect.extent.x, rect.extent.y);
}

void wrei_region::subtract(rect2i32 rect)
{
    auto x = rect.origin.x;
    auto y = rect.origin.y;
    auto width = rect.extent.x;
    auto height = rect.extent.y;
    pixman_region32_union_rect(&region, &region, x, y, width, height);

    pixman_region32_t subtrahend;
    pixman_region32_init_rect(&subtrahend, x, y, width, height);
    pixman_region32_subtract(&region, &region, &subtrahend);
    pixman_region32_fini(&subtrahend);
}

bool wrei_region::contains(vec2i32 point) const
{
    pixman_box32_t box;
    return pixman_region32_contains_point(&region, point.x, point.y, &box);
}

bool wrei_region::contains(rect2i32 rect) const
{
    pixman_box32 box{
        .x1 = rect.origin.x,
        .y1 = rect.origin.y,
        .x2 = rect.origin.x + rect.extent.x,
        .y2 = rect.origin.y + rect.extent.y,
    };
    auto overlap = pixman_region32_contains_rectangle(&region, &box);

    return overlap == PIXMAN_REGION_IN;
}

vec2f64 wrei_region::constrain(vec2f64 point) const
{
    double closest_dist = INFINITY;
    vec2f64 closest = {};

    i32 nrects;
    auto* rects = pixman_region32_rectangles(&region, &nrects);

    for (i32 i = 0; i < nrects; ++i) {
        auto& _rect = rects[i];
        auto rect = rect2f64{{_rect.x1, _rect.y1}, {_rect.x2 - _rect.x1 - 1, _rect.y2 - _rect.y1 - 1}};
        if (wrei_rect_contains(rect, point)) {
            return point;
        } else {
            auto pos = wrei_rect_clamp_point(rect, point);
            auto dist = glm::distance(pos, point);
            if (dist < closest_dist) {
                closest = pos;
                closest_dist = dist;
            }
        }
    }

    return closest;
}
