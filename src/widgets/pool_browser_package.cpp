#include "pool_browser_package.hpp"
#include "pool/pool.hpp"
#include "tag_entry.hpp"
#include <set>

namespace horizon {
PoolBrowserPackage::PoolBrowserPackage(Pool *p) : PoolBrowser(p)
{
    construct();
    name_entry = create_search_entry("Name");
    manufacturer_entry = create_search_entry("Manufacturer");
    tag_entry = create_tag_entry("Tags");
    install_pool_item_source_tooltip();
}

Glib::RefPtr<Gtk::ListStore> PoolBrowserPackage::create_list_store()
{
    return Gtk::ListStore::create(list_columns);
}

void PoolBrowserPackage::create_columns()
{
    {
        auto col = append_column_with_item_source_cr("Package", list_columns.name, Pango::ELLIPSIZE_END);
        col->set_resizable(true);
        col->set_expand(true);
        col->set_min_width(200);
        install_column_tooltip(*col, list_columns.name);
    }
    {
        auto col = append_column("Manufacturer", list_columns.manufacturer, Pango::ELLIPSIZE_END);
        col->set_resizable(true);
        col->set_min_width(30);
    }
    treeview->append_column("Pads", list_columns.n_pads);
    {
        auto col = append_column("Tags", list_columns.tags, Pango::ELLIPSIZE_END);
        col->set_resizable(true);
        col->set_min_width(100);
        install_column_tooltip(*col, list_columns.tags);
    }
    path_column = append_column("Path", list_columns.path, Pango::ELLIPSIZE_START);
    install_column_tooltip(*path_column, list_columns.path);
}

void PoolBrowserPackage::add_sort_controller_columns()
{
    sort_controller->add_column(0, "packages.name");
    sort_controller->add_column(1, "packages.manufacturer");
}

void PoolBrowserPackage::search()
{
    prepare_search();

    Gtk::TreeModel::Row row;
    std::string name_search = name_entry->get_text();
    std::string manufacturer_search = manufacturer_entry->get_text();

    auto tags = tag_entry->get_tags();
    std::ostringstream qs;
    qs << "SELECT packages.uuid, packages.name, packages.manufacturer, "
          "packages.n_pads, tags_view.tags, packages.filename, packages.pool_uuid, packages.overridden "
          "FROM packages "
          "LEFT JOIN tags_view ON tags_view.uuid = packages.uuid AND tags_view.type = 'package' ";

    if (tags.size()) {
        qs << "INNER JOIN (SELECT uuid FROM tags WHERE tags.tag IN (";
        int i = 0;
        for (const auto &it : tags) {
            (void)sizeof it;
            qs << "$tag" << i << ", ";
            i++;
        }
        qs << "'') AND tags.type = 'package' "
              "GROUP by tags.uuid HAVING count(*) >= $ntags) as x ON x.uuid = packages.uuid ";
    }
    qs << "WHERE packages.name LIKE $name AND packages.manufacturer LIKE $manufacturer ";
    qs << sort_controller->get_order_by();

    SQLite::Query q(pool->db, qs.str());
    q.bind("$name", "%" + name_search + "%");
    q.bind("$manufacturer", "%" + manufacturer_search + "%");
    int i = 0;
    for (const auto &it : tags) {
        q.bind(("$tag" + std::to_string(i)).c_str(), it);
        i++;
    }
    if (tags.size())
        q.bind("$ntags", tags.size());

    if (show_none) {
        row = *(store->append());
        row[list_columns.uuid] = UUID();
        row[list_columns.name] = "none";
        row[list_columns.manufacturer] = "none";
    }
    try {
        while (q.step()) {
            row = *(store->append());
            row[list_columns.uuid] = q.get<std::string>(0);
            row[list_columns.name] = q.get<std::string>(1);
            row[list_columns.manufacturer] = q.get<std::string>(2);
            row[list_columns.n_pads] = q.get<int>(3);
            row[list_columns.tags] = q.get<std::string>(4);
            row[list_columns.path] = q.get<std::string>(5);
            row[list_columns.source] = pool_item_source_from_db(q.get<std::string>(6), q.get<int>(7));
        }
        set_busy(false);
    }
    catch (SQLite::Error &e) {
        if (e.rc == SQLITE_BUSY) {
            set_busy(true);
        }
        else {
            throw;
        }
    }

    finish_search();
}

UUID PoolBrowserPackage::uuid_from_row(const Gtk::TreeModel::Row &row)
{
    return row[list_columns.uuid];
}
PoolBrowser::PoolItemSource PoolBrowserPackage::pool_item_source_from_row(const Gtk::TreeModel::Row &row)
{
    return row[list_columns.source];
}
} // namespace horizon
