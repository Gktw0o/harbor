#include "core/island/dock_model.h"

#include <algorithm>
#include <iterator>

namespace harbor::island
{

std::string FallbackName(std::string_view aumid, std::string_view exe)
{
    if (!exe.empty())
    {
        const auto slash = exe.find_last_of("\\/");
        auto name = slash == std::string_view::npos ? exe : exe.substr(slash + 1);
        if (name.size() > 4 && windows::LowerAscii(name.substr(name.size() - 4)) == ".exe")
        {
            name.remove_suffix(4);
        }
        if (!name.empty())
        {
            return std::string(name);
        }
    }
    return aumid.empty() ? std::string("App") : std::string(aumid);
}

bool IsPinnedAs(const ipc::DockItem& pin, const RunningApp& app)
{
    const auto target = windows::LowerAscii(pin.target);
    if (app.info && app.info->pinned && app.info->pinned->kind == pin.kind &&
        windows::LowerAscii(app.info->pinned->target) == target)
    {
        return true;
    }
    using Kind = windows::AppIdentity::Kind;
    return app.identity.key == target &&
           ((pin.kind == ipc::DockItemKind::App && app.identity.kind == Kind::Aumid) ||
            (pin.kind == ipc::DockItemKind::Path && app.identity.kind == Kind::Exe));
}

DockModel BuildDockModel(std::span<const ipc::DockItem> pins, std::span<const RunningApp> apps,
                         std::chrono::milliseconds now)
{
    DockModel model;
    model.pinned = pins.size();
    for (std::size_t i = 0; i < pins.size(); ++i)
    {
        model.slots.push_back(
            {.kind = DockSlot::Kind::Pin, .pin = i, .name = pins[i].name, .icon = pins[i].icon});
    }

    std::vector<DockSlot> unpinned;
    for (const auto& app : apps)
    {
        const auto pin = std::find_if(pins.begin(), pins.end(),
                                      [&](const ipc::DockItem& item) { return IsPinnedAs(item, app); });
        if (pin != pins.end())
        {
            auto& slot = model.slots[static_cast<std::size_t>(pin - pins.begin())];
            slot.apps.push_back(app.id);
            slot.marks.running = true;
            slot.marks.attention = slot.marks.attention || app.attention;
            continue;
        }
        DockSlot slot{.kind = DockSlot::Kind::App,
                      .apps = {app.id},
                      .marks = {.running = true, .attention = app.attention}};
        if (app.info)
        {
            slot.name = app.info->name;
            slot.icon = app.info->icon;
        }
        else if (now >= app.seen + kLookupWait)
        {
            slot.name = app.fallbackName;
        }
        else
        {
            const auto due = app.seen + kLookupWait;
            model.rebuildAt = model.rebuildAt ? std::min(*model.rebuildAt, due) : due;
            continue;
        }
        unpinned.push_back(std::move(slot));
    }
    model.slots.insert(model.slots.end(), std::make_move_iterator(unpinned.begin()),
                       std::make_move_iterator(unpinned.end()));
    return model;
}

} // namespace harbor::island
