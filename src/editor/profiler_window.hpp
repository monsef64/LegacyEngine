#pragma once
#include "core/profiler.hpp"
#include "ui/mygui_ui.hpp"
inline void drawProfiler(FrameProfiler& profiler, bool& visible) {
    if (!visible) {
        return;
    }
    ui::setNextWindowSize(ui::Vec2(690, 530), ui::Cond_FirstUseEver);
    if (ui::beginWindow("Profiler", &visible)) {
        ui::checkbox("Pause sampling", &profiler.paused);
        const auto frame = profiler.stats();
        ui::sameLine();
        ui::text("%zu / %zu frames", profiler.count, profiler.capacity);
        ui::text("Frame: %.2f ms | Average: %.2f ms (%.0f FPS) | Peak: %.2f ms",
                    frame.last,
                    frame.average,
                    frame.average > 0 ? 1000 / frame.average : 0,
                    frame.peak);
        float plot[FrameProfiler::capacity]{};
        for (std::size_t i = 0; i < profiler.count; ++i) {
            plot[i] = float(profiler
                                .history[(profiler.next + profiler.capacity - profiler.count + i) %
                                         profiler.capacity]
                                .total);
        }
        ui::plotLines("Frame ms",
                         plot,
                         int(profiler.count),
                         0,
                         nullptr,
                         0,
                         std::max(20.0f, float(frame.peak)),
                         ui::Vec2(0, 75));
        ui::textWrapped(
            "CPU wall time, including driver calls and vsync wait; not GPU execution time. Render sub-rows are nested within Scene/Game and must not be added to the frame total.");
        if (ui::beginTable("Costs",
                              5,
                              ui::TableFlags_RowBg | ui::TableFlags_Borders |
                                  ui::TableFlags_SizingStretchProp)) {
            for (const char* title : {"Subsystem", "Last ms", "Avg ms", "Peak ms", "Avg frame %"}) {
                ui::tableSetupColumn(title);
            }
            ui::tableHeadersRow();
            for (std::size_t i = 0; i < FrameProfiler::parts; ++i) {
                auto stats = profiler.stats(int(i));
                ui::tableNextRow();
                ui::tableNextColumn();
                ui::textUnformatted(profileNames[i]);
                ui::tableNextColumn();
                ui::text("%.3f", stats.last);
                ui::tableNextColumn();
                ui::text("%.3f", stats.average);
                ui::tableNextColumn();
                ui::text("%.3f", stats.peak);
                ui::tableNextColumn();
                ui::text("%.1f%%", frame.average > 0 ? 100 * stats.average / frame.average : 0);
            }
            ui::endTable();
        }
    }
    ui::endWindow();
}
