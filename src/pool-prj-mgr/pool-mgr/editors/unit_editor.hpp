#pragma once
#include <gtkmm.h>
#include "common/common.hpp"
#include "editor_interface.hpp"

namespace horizon {

class UnitEditor : public Gtk::Box, public PoolEditorInterface {
    friend class PinEditor;

public:
    UnitEditor(BaseObjectType *cobject, const Glib::RefPtr<Gtk::Builder> &x, class Unit *u, class Pool *p);
    static UnitEditor *create(class Unit *u, class Pool *p);

    virtual ~UnitEditor(){};

private:
    class Unit *unit = nullptr;
    Gtk::Entry *name_entry = nullptr;
    Gtk::Entry *manufacturer_entry = nullptr;
    Gtk::ListBox *pins_listbox = nullptr;
    Gtk::ToolButton *refresh_button = nullptr;
    Gtk::ToolButton *add_button = nullptr;
    Gtk::ToolButton *delete_button = nullptr;

    Glib::RefPtr<Gtk::SizeGroup> sg_direction;
    Glib::RefPtr<Gtk::SizeGroup> sg_name;
    Glib::RefPtr<Gtk::SizeGroup> sg_names;

    void handle_add();
    void handle_delete();
    void sort();
    void handle_activate(class PinEditor *ed);

    Pool *pool;
};
} // namespace horizon
