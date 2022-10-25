#pragma once

#include <pangolin/utils/shared.h>
#include <pangolin/maths/min_max.h>
#include <Eigen/Core>

namespace pangolin
{

// Forward declarations
struct Widget;
struct PanelGroup;


////////////////////////////////////////////////////////////////////
/// Represents a client area in a window with layout handling
///
struct Panel : std::enable_shared_from_this<Panel>
{
    struct Absolute {int pixels = 100;};
    struct Parts {double ratio = 1.0; };
    struct RenderParams{
        MinMax<Eigen::Vector2i> region;
    };

    using Dim = std::variant<Parts,Absolute>;
    using Size = Eigen::Vector<Dim,2>;

    virtual void renderIntoRegion(const RenderParams&) = 0;

    struct Params {
        std::string title = "";
        Size size_hint = {Parts{1}, Parts{1}};
    };
    static Shared<Panel> Create(Params);
};

}
