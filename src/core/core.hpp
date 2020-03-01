#pragma once
#include "canvas/selectables.hpp"
#include "canvas/target.hpp"
#include "common/layer.hpp"
#include "common/object_descr.hpp"
#include "common/keepout.hpp"
#include "tool_data.hpp"
#include "nlohmann/json_fwd.hpp"
#include "pool/pool.hpp"
#include "pool/symbol.hpp"
#include <gdk/gdkkeysyms.h>
#include <iostream>
#include <memory>
#include <sigc++/sigc++.h>
#include "tool_id.hpp"
#include "tool.hpp"
#include "document/idocument.hpp"

namespace horizon {


/**
 * Where Tools and and documents meet.
 * The Core provides a unified interface for Tools to access
 * the objects common to all documents (whatever is being edited).
 * It also provides the property interface for the property editor.
 *
 * A Core always stores to copies of the document, one of which is
 * the working copy. Tools always operate on this one. Tools use Core::commit()
 * to
 * commit their changes by replacing the non-working document with the working
 * document.
 * Core::revert() does the opposite thing by replacing the working document with
 * the non-working document, thereby discarding the changes made to the working
 * copy.
 * Usually, calling Core::commit() or Core::revert() is the last thing a Tool
 * does before
 * finishing.
 *
 * After a Tool has finished its work by returning ToolResponse::end(), the Core
 * will
 * initiate a rebuild. For CoreSchematic, a rebuild will update the Schematic
 * according to its Block.
 *
 * The Core also handles undo/redo by storing a full copy for each step.
 */
class Core : public virtual IDocument {
public:
    bool has_object_type(ObjectType ty) const override
    {
        return false;
    }

    class Junction *insert_junction(const UUID &uu) override;
    class Junction *get_junction(const UUID &uu) override;
    void delete_junction(const UUID &uu) override;

    class Line *insert_line(const UUID &uu) override;
    class Line *get_line(const UUID &uu) override;
    void delete_line(const UUID &uu) override;

    class Arc *insert_arc(const UUID &uu) override;
    class Arc *get_arc(const UUID &uu) override;
    void delete_arc(const UUID &uu) override;

    class Text *insert_text(const UUID &uu) override;
    class Text *get_text(const UUID &uu) override;
    void delete_text(const UUID &uu) override;

    class Polygon *insert_polygon(const UUID &uu) override;
    class Polygon *get_polygon(const UUID &uu) override;
    void delete_polygon(const UUID &uu) override;

    class Hole *insert_hole(const UUID &uu) override;
    class Hole *get_hole(const UUID &uu) override;
    void delete_hole(const UUID &uu) override;

    class Dimension *insert_dimension(const UUID &uu) override;
    class Dimension *get_dimension(const UUID &uu) override;
    void delete_dimension(const UUID &uu) override;

    class Keepout *insert_keepout(const UUID &uu) override;
    class Keepout *get_keepout(const UUID &uu) override;
    void delete_keepout(const UUID &uu) override;

    std::vector<Line *> get_lines() override;
    std::vector<Arc *> get_arcs() override;
    std::vector<Keepout *> get_keepouts() override;

    class Block *get_block() override
    {
        return nullptr;
    }

    class Rules *get_rules() override
    {
        return nullptr;
    }

    class Pool *get_pool() override
    {
        return m_pool;
    }

    /**
     * Expands the non-working document.
     * And copies the non-working document to the working document.
     */
    virtual void rebuild(bool from_undo = false);
    ToolResponse tool_begin(ToolID tool_id, const ToolArgs &args, class ImpInterface *imp, bool transient = false);
    ToolResponse tool_update(const ToolArgs &args);
    std::pair<bool, bool> tool_can_begin(ToolID tool_id, const std::set<SelectableRef> &selection);
    bool tool_handles_esc();
    void save();
    void autosave();
    virtual void delete_autosave() = 0;

    void undo();
    void redo();

    bool can_undo() const;
    bool can_redo() const;

    inline bool tool_is_active()
    {
        return tool != nullptr;
    }

    virtual bool set_property(ObjectType type, const UUID &uu, ObjectProperty::ID property,
                              const class PropertyValue &value);
    virtual bool get_property(ObjectType type, const UUID &uu, ObjectProperty::ID property, class PropertyValue &value);
    virtual bool get_property_meta(ObjectType type, const UUID &uu, ObjectProperty::ID property,
                                   class PropertyMeta &meta);

    virtual std::string get_display_name(ObjectType type, const UUID &uu) override;
    virtual std::string get_display_name(ObjectType type, const UUID &uu, const UUID &sheet) override;

    void set_property_begin();
    void set_property_commit();
    bool get_property_transaction() const;

    class LayerProvider *get_layer_provider() override
    {
        return nullptr;
    };

    /**
     * @returns the current document's meta information.
     * Meta information contains grid spacing and layer setup.
     */
    virtual json get_meta();

    virtual void update_rules()
    {
    }

    virtual std::pair<Coordi, Coordi> get_bbox() = 0;

    virtual ~Core()
    {
    }
    std::set<SelectableRef> &get_tool_selection();
    Pool *m_pool;

    bool get_needs_save() const;
    void set_needs_save();

    virtual const std::string &get_filename() const = 0;

    typedef sigc::signal<void, ToolID> type_signal_tool_changed;
    type_signal_tool_changed signal_tool_changed()
    {
        return s_signal_tool_changed;
    }
    typedef sigc::signal<void> type_signal_rebuilt;
    type_signal_rebuilt signal_rebuilt()
    {
        return s_signal_rebuilt;
    }
    /**
     * Gets emitted right before saving. Gives the Imp an opportunity to write
     * additional
     * information to the document.
     */
    type_signal_rebuilt signal_save()
    {
        return s_signal_save;
    }

    type_signal_rebuilt signal_modified()
    {
        return s_signal_modified;
    }

    type_signal_rebuilt signal_can_undo_redo()
    {
        return s_signal_can_undo_redo;
    }

    typedef sigc::signal<void, bool> type_signal_needs_save;
    type_signal_needs_save signal_needs_save()
    {
        return s_signal_needs_save;
    }

    typedef sigc::signal<json, ToolID> type_signal_load_tool_settings;
    type_signal_load_tool_settings signal_load_tool_settings()
    {
        return s_signal_load_tool_settings;
    }

    typedef sigc::signal<void, ToolID, json> type_signal_save_tool_settings;
    type_signal_save_tool_settings signal_save_tool_settings()
    {
        return s_signal_save_tool_settings;
    }

    virtual void reload_pool()
    {
    }

protected:
    virtual std::map<UUID, Junction> *get_junction_map()
    {
        return nullptr;
    }
    virtual std::map<UUID, Line> *get_line_map()
    {
        return nullptr;
    }
    virtual std::map<UUID, Arc> *get_arc_map()
    {
        return nullptr;
    }
    virtual std::map<UUID, Text> *get_text_map()
    {
        return nullptr;
    }
    virtual std::map<UUID, Polygon> *get_polygon_map()
    {
        return nullptr;
    }
    virtual std::map<UUID, Hole> *get_hole_map()
    {
        return nullptr;
    }
    virtual std::map<UUID, Dimension> *get_dimension_map()
    {
        return nullptr;
    }
    virtual std::map<UUID, Keepout> *get_keepout_map()
    {
        return nullptr;
    }

    std::unique_ptr<ToolBase> tool = nullptr;
    type_signal_tool_changed s_signal_tool_changed;
    type_signal_rebuilt s_signal_rebuilt;
    type_signal_rebuilt s_signal_save;
    type_signal_rebuilt s_signal_modified;
    type_signal_rebuilt s_signal_can_undo_redo;
    type_signal_needs_save s_signal_needs_save;
    type_signal_load_tool_settings s_signal_load_tool_settings;
    type_signal_save_tool_settings s_signal_save_tool_settings;
    bool needs_save = false;
    void set_needs_save(bool v);

    class HistoryItem {
    public:
        // Symbol sym;
        // HistoryItem(const Symbol &s): sym(s) {}
        std::string comment;
        virtual ~HistoryItem()
        {
        }
    };
    std::deque<std::unique_ptr<HistoryItem>> history;
    int history_current = -1;
    virtual void history_push() = 0;
    virtual void history_load(unsigned int i) = 0;
    void history_clear();
    void history_trim();

    bool property_transaction = false;

    void layers_to_meta(class PropertyMeta &meta);
    void get_placement(const Placement &placement, class PropertyValue &value, ObjectProperty::ID property);
    void set_placement(Placement &placement, const class PropertyValue &value, ObjectProperty::ID property);

    virtual void save(const std::string &suffix) = 0;
    static const std::string autosave_suffix;
    json get_meta_from_file(const std::string &filename);

private:
    std::unique_ptr<ToolBase> create_tool(ToolID tool_id);
    std::set<SelectableRef> tool_selection;
    void maybe_end_tool(const ToolResponse &r);
};
} // namespace horizon
