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
