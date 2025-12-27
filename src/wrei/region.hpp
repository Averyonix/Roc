#include "util.hpp"

template<typename T>
struct wrei_region
{
    std::vector<wrei_aabb<T>> aabbs;

// -----------------------------------------------------------------------------

    wrei_region() = default;

    wrei_region(wrei_aabb<T> aabb)
        : aabbs{aabb}
    {}

// -----------------------------------------------------------------------------

    wrei_region(const wrei_region& other)
        : aabbs(other.aabbs)
    {}

    wrei_region& operator=(const wrei_region& other)
    {
        if (this != &other) {
            aabbs = other.aabbs;
        }
        return *this;
    }

// -----------------------------------------------------------------------------

    wrei_region(wrei_region&& other)
        : aabbs(std::move(other.aabbs))
    {}

    wrei_region& operator=(wrei_region&& other)
    {
        if (this != &other) {
            aabbs = std::move(other.aabbs);
        }
        return *this;
    }

// -----------------------------------------------------------------------------

    void clear()
    {
        aabbs.clear();
    }

    bool empty() const
    {
        return aabbs.empty();
    }

    void add(wrei_aabb<T> aabb)
    {
        aabbs.emplace_back(aabb);
    }

    void subtract(wrei_aabb<T> subtrahend)
    {
        usz prev_size = aabbs.size();
        usz inplace = 0;

        for (usz i = 0; i < prev_size; ++i) {

            std::array<wrei_aabb<T>, 4> split;
            u32 count = wrei_aabb_subtract(aabbs[i], subtrahend, split.data());

            // Update first aabb in-place
            if (count > 0) {
                aabbs[inplace++] = split[0];
            }

            // Append remaining aabbs to end
            if (count > 1) {
                aabbs.insert_range(aabbs.end(), std::span{split}.subspan(1, count - 1));
            }
        }

        // Clean if hole left in list
        if (inplace < prev_size) {
            usz extra = aabbs.size() - prev_size;
            usz to_delete = prev_size - inplace;
            usz to_move = std::min(to_delete, extra);
            std::copy(aabbs.end() - to_move, aabbs.end(), aabbs.begin() + inplace);
            aabbs.erase(aabbs.begin() + inplace + extra);
        }
    }

    template<typename T2>
    bool contains(wrei_vec<2, T2> point) const
    {
        for (auto aabb : aabbs) {
            if (wrei_aabb_contains<T2>(aabb, point)) {
                return true;
            }
        }
        return false;
    }

    template<typename T2>
    bool contains(wrei_aabb<T2> needle) const
    {
        for (auto aabb : aabbs) {
            wrei_aabb<T2> overlap;
            wrei_aabb_intersects<T2>(aabb, needle, &overlap);
            if (overlap == needle) return true;
        }
        return false;
    }

    template<typename T2>
    wrei_vec<2, T2> constrain(wrei_vec<2, T2> point) const
    {
        double closest_dist = INFINITY;
        wrei_vec<2, T2> closest = {};

        for (auto aabb : aabbs) {
            auto pos = wrei_aabb_clamp_point<T2>(aabb, point);
            if (pos == point) return point;

            auto dist = glm::distance(pos, point);
            if (dist < closest_dist) {
                closest = pos;
                closest_dist = dist;
            }
        }

        return closest;
    }
};

// -----------------------------------------------------------------------------

using region2i32 = wrei_region<i32>;
