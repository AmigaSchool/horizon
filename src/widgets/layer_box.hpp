#pragma once
#include <gtkmm.h>
#include "canvas/layer_display.hpp"
#include "canvas/canvas_gl.hpp"

namespace horizon {
class LayerBox : public Gtk::Box {
public:
    LayerBox(class LayerProvider &lp, bool show_title = true);
    void set_appearance(const Appearance &a);
    void update();
    Glib::PropertyProxy<int> property_work_layer()
    {
        return p_property_work_layer.get_proxy();
    }
    typedef sigc::signal<void, int, LayerDisplay> type_signal_set_layer_display;
    type_signal_set_layer_display signal_set_layer_display()
    {
        return s_signal_set_layer_display;
    }


    Glib::PropertyProxy<float> property_layer_opacity()
    {
        return p_property_layer_opacity.get_proxy();
    }
    Glib::PropertyProxy<CanvasGL::HighlightMode> property_highlight_mode()
    {
        return p_property_highlight_mode.get_proxy();
    }
    Glib::PropertyProxy<CanvasGL::LayerMode> property_layer_mode()
    {
        return p_property_layer_mode.get_proxy();
    }
    void set_layer_display(int layer, const LayerDisplay &ld);

    json serialize();
    void load_from_json(const json &j);

private:
    class LayerProvider &lp;
    Appearance appearance;
    void update_colors();

    Gtk::ListBox *lb = nullptr;

    Glib::Property<int> p_property_work_layer;
    Glib::Property<float> p_property_layer_opacity;
    Glib::Property<CanvasGL::HighlightMode> p_property_highlight_mode;
    Glib::Property<CanvasGL::LayerMode> p_property_layer_mode;
    type_signal_set_layer_display s_signal_set_layer_display;
    void emit_layer_display(class LayerBoxRow *row);
    void update_work_layer();

    Glib::RefPtr<Glib::Binding> binding_select_work_layer_only;
    Glib::RefPtr<Glib::Binding> binding_layer_opacity;
};
} // namespace horizon
