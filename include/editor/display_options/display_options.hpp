#pragma once
#include "editor/GUI/named_container.hpp"
#include "editor/GUI/utils.hpp"
#include "editor/control_state.hpp"
#include "editor/GUI/toggle.hpp"


struct DisplayOption : public GUI::NamedContainer
{
    ControlState& control_state;

    bool draw_ants    = false;
    bool draw_markers = false;
    bool draw_density = false;
    bool draw_scent   = false;

    explicit
    DisplayOption(ControlState& control_state_)
        : GUI::NamedContainer("Display Options", GUI::Container::Orientation::Horizontal)
        , control_state(control_state_)
    {
        size_type.y = GUI::Size::FitContent;
        auto ants = create<GUI::NamedToggle>("Draw Ants");
        watch(ants, [this, ants](){
            draw_ants = ants->getState();
            notifyChanged();
        });
        ants->setState(true);
        GUI::NamedContainer::addItem(ants);

        auto markers = create<GUI::NamedToggle>("Draw Markers");
        GUI::NamedContainer::addItem(markers);
        auto density = create<GUI::NamedToggle>("Draw Density");
        GUI::NamedContainer::addItem(density);
        auto scent = create<GUI::NamedToggle>("Draw Food Scent");
        GUI::NamedContainer::addItem(scent);

        auto update_states = [this, markers, density, scent](){
            draw_markers = markers->getState();
            draw_density = density->getState();
            draw_scent   = scent->getState();
            notifyChanged();
        };

        watch(markers, [markers, density, scent, update_states](){
            if (markers->getState()) {
                density->setState(false);
                scent->setState(false);
            }
            update_states();
        });

        watch(density, [markers, density, scent, update_states](){
            if (density->getState()) {
                markers->setState(false);
                scent->setState(false);
            }
            update_states();
        });

        watch(scent, [markers, density, scent, update_states](){
            if (scent->getState()) {
                markers->setState(false);
                density->setState(false);
            }
            update_states();
        });
    }
};

