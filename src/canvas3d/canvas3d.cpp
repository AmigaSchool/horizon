#include "canvas3d.hpp"
#include "board/board.hpp"
#include "board/board_layers.hpp"
#include "canvas/gl_util.hpp"
#include "common/hole.hpp"
#include "common/polygon.hpp"
#include "logger/logger.hpp"
#include "poly2tri/poly2tri.h"
#include "util/step_importer.hpp"
#include "util/util.hpp"
#include "pool/pool_manager.hpp"
#include "util/min_max_accumulator.hpp"
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <iostream>
#include <thread>

namespace horizon {

Canvas3D::Canvas3D()
    : Gtk::GLArea(), cover_renderer(this), wall_renderer(this), face_renderer(this), background_renderer(this),
      center(0)
{
    add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::BUTTON_MOTION_MASK | Gdk::SCROLL_MASK
               | Gdk::SMOOTH_SCROLL_MASK);

    models_loading_dispatcher.connect([this] {
        package_height_max = 0;
        for (const auto &it : face_vertex_buffer) {
            package_height_max = std::max(it.z, package_height_max);
        }
        request_push();
        s_signal_models_loading.emit(false);
    });

    gesture_drag = Gtk::GestureDrag::create(*this);
    gesture_drag->signal_begin().connect(sigc::mem_fun(*this, &Canvas3D::drag_gesture_begin_cb));
    gesture_drag->signal_update().connect(sigc::mem_fun(*this, &Canvas3D::drag_gesture_update_cb));
    gesture_drag->set_propagation_phase(Gtk::PHASE_CAPTURE);
    gesture_drag->set_touch_only(true);

    gesture_zoom = Gtk::GestureZoom::create(*this);
    gesture_zoom->signal_begin().connect(sigc::mem_fun(*this, &Canvas3D::zoom_gesture_begin_cb));
    gesture_zoom->signal_update().connect(sigc::mem_fun(*this, &Canvas3D::zoom_gesture_update_cb));
    gesture_zoom->set_propagation_phase(Gtk::PHASE_CAPTURE);

    gesture_rotate = Gtk::GestureRotate::create(*this);
    gesture_rotate->signal_begin().connect(sigc::mem_fun(*this, &Canvas3D::rotate_gesture_begin_cb));
    gesture_rotate->signal_update().connect(sigc::mem_fun(*this, &Canvas3D::rotate_gesture_update_cb));
    gesture_rotate->set_propagation_phase(Gtk::PHASE_CAPTURE);
}

glm::vec2 Canvas3D::get_center_shift(const glm::vec2 &shift) const
{
    return glm::rotate(glm::mat2(1, 0, 0, sin(glm::radians(cam_elevation))) * shift * 0.1218f * cam_distance / 105.f,
                       glm::radians(cam_azimuth - 90));
}


void Canvas3D::set_appearance(const Appearance &a)
{
    appearance = a;
    queue_draw();
}

void Canvas3D::on_size_allocate(Gtk::Allocation &alloc)
{
    width = alloc.get_width();
    height = alloc.get_height();
    needs_resize = true;
    if (needs_view_all) {
        view_all();
        needs_view_all = false;
    }

    // chain up
    Gtk::GLArea::on_size_allocate(alloc);
}

bool Canvas3D::on_button_press_event(GdkEventButton *button_event)
{
    if (button_event->button == 2 || (button_event->button == 1 && (button_event->state & Gdk::SHIFT_MASK))) {
        pan_mode = PanMode::MOVE;
        pointer_pos_orig = {button_event->x, button_event->y};
        center_orig = center;
    }
    else if (button_event->button == 1) {
        pan_mode = PanMode::ROTATE;
        pointer_pos_orig = {button_event->x, button_event->y};
        cam_elevation_orig = cam_elevation;
        cam_azimuth_orig = cam_azimuth;
    }
    return Gtk::GLArea::on_button_press_event(button_event);
}

void Canvas3D::fix_cam_elevation()
{
    while (cam_elevation >= 360)
        cam_elevation -= 360;
    while (cam_elevation < 0)
        cam_elevation += 360;
    if (cam_elevation > 180)
        cam_elevation -= 360;
}

bool Canvas3D::on_motion_notify_event(GdkEventMotion *motion_event)
{
    auto delta = glm::mat2(1, 0, 0, -1) * (glm::vec2(motion_event->x, motion_event->y) - pointer_pos_orig);
    if (pan_mode == PanMode::ROTATE) {
        cam_azimuth = cam_azimuth_orig - (delta.x / width) * 360;
        cam_elevation = cam_elevation_orig - (delta.y / height) * 90;
        fix_cam_elevation();
        queue_draw();
    }
    else if (pan_mode == PanMode::MOVE) {
        center = center_orig + get_center_shift(delta);
        queue_draw();
    }
    return Gtk::GLArea::on_motion_notify_event(motion_event);
}

bool Canvas3D::on_button_release_event(GdkEventButton *button_event)
{
    pan_mode = PanMode::NONE;
    return Gtk::GLArea::on_button_release_event(button_event);
}

void Canvas3D::drag_gesture_begin_cb(GdkEventSequence *seq)
{
    if (pan_mode != PanMode::NONE) {
        gesture_drag->set_state(Gtk::EVENT_SEQUENCE_DENIED);
    }
    else {
        gesture_drag_center_orig = center;
        gesture_drag->set_state(Gtk::EVENT_SEQUENCE_CLAIMED);
    }
}
void Canvas3D::drag_gesture_update_cb(GdkEventSequence *seq)
{
    double x, y;
    if (gesture_drag->get_offset(x, y)) {
        center = gesture_drag_center_orig + get_center_shift({x, -y});
        queue_draw();
    }
}

void Canvas3D::zoom_gesture_begin_cb(GdkEventSequence *seq)
{
    if (pan_mode != PanMode::NONE) {
        gesture_zoom->set_state(Gtk::EVENT_SEQUENCE_DENIED);
        return;
    }
    gesture_zoom_cam_dist_orig = cam_distance;
    gesture_zoom->set_state(Gtk::EVENT_SEQUENCE_CLAIMED);
}

void Canvas3D::zoom_gesture_update_cb(GdkEventSequence *seq)
{
    auto delta = gesture_zoom->get_scale_delta();
    cam_distance = gesture_zoom_cam_dist_orig / delta;
    queue_draw();
}

void Canvas3D::rotate_gesture_begin_cb(GdkEventSequence *seq)
{
    if (pan_mode != PanMode::NONE) {
        gesture_zoom->set_state(Gtk::EVENT_SEQUENCE_DENIED);
        return;
    }
    gesture_rotate_cam_azimuth_orig = cam_azimuth;
    gesture_rotate_cam_elevation_orig = cam_elevation;
    double cx, cy;
    gesture_rotate->get_bounding_box_center(cx, cy);
    gesture_rotate_pos_orig = glm::vec2(cx, cy);
    gesture_zoom->set_state(Gtk::EVENT_SEQUENCE_CLAIMED);
}

void Canvas3D::rotate_gesture_update_cb(GdkEventSequence *seq)
{
    auto delta = gesture_rotate->get_angle_delta();
    if (cam_elevation < 0)
        delta *= -1;
    cam_azimuth = gesture_rotate_cam_azimuth_orig + glm::degrees(delta);
    inc_cam_azimuth(0);
    double cx, cy;
    gesture_rotate->get_bounding_box_center(cx, cy);
    auto dy = cy - gesture_rotate_pos_orig.y;
    cam_elevation = gesture_rotate_cam_elevation_orig + (dy / height) * 180;
    fix_cam_elevation();
    queue_draw();
}

int Canvas3D::_animate_step(GdkFrameClock *frame_clock)
{
    auto r = zoom_animator.step(gdk_frame_clock_get_frame_time(frame_clock) / 1e6);
    if (!r) { // should stop
        return G_SOURCE_REMOVE;
    }
    auto s = zoom_animator.get_s();

    cam_distance = zoom_animation_cam_dist_orig * pow(1.5, s);
    queue_draw();

    if (std::abs((s - zoom_animator.target) / std::max(std::abs(zoom_animator.target), 1.f)) < .005) {
        cam_distance = zoom_animation_cam_dist_orig * pow(1.5, zoom_animator.target);
        queue_draw();
        zoom_animator.stop();
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static int tick_cb(GtkWidget *cwidget, GdkFrameClock *frame_clock, gpointer user_data)
{
    Gtk::Widget *widget = Glib::wrap(cwidget);
    auto canvas = dynamic_cast<Canvas3D *>(widget);
    return canvas->_animate_step(frame_clock);
}


bool Canvas3D::on_scroll_event(GdkEventScroll *scroll_event)
{

    float inc = 0;
    float factor = 1;
    if (scroll_event->state & Gdk::SHIFT_MASK)
        factor = .5;
    if (scroll_event->direction == GDK_SCROLL_UP) {
        inc = -1;
    }
    else if (scroll_event->direction == GDK_SCROLL_DOWN) {
        inc = 1;
    }
    else if (scroll_event->direction == GDK_SCROLL_SMOOTH) {
        gdouble sx, sy;
        gdk_event_get_scroll_deltas((GdkEvent *)scroll_event, &sx, &sy);
        inc = sy;
    }
    inc *= factor;
    if (smooth_zoom) {
        if (inc == 0)
            return Gtk::GLArea::on_scroll_event(scroll_event);
        if (!zoom_animator.is_running()) {
            zoom_animator.start();
            zoom_animation_cam_dist_orig = cam_distance;
            gtk_widget_add_tick_callback(GTK_WIDGET(gobj()), &tick_cb, nullptr, nullptr);
        }
        zoom_animator.target += inc;
    }
    else {
        cam_distance *= pow(1.5, inc);
        queue_draw();
    }


    return Gtk::GLArea::on_scroll_event(scroll_event);
}

void Canvas3D::inc_cam_azimuth(float v)
{
    cam_azimuth += v;
    while (cam_azimuth < 0)
        cam_azimuth += 360;

    while (cam_azimuth > 360)
        cam_azimuth -= 360;
    queue_draw();
}

static const float magic_number = 0.4143;

void Canvas3D::view_all()
{
    if (!brd)
        return;

    const auto &vertices = ca.get_layer(BoardLayers::L_OUTLINE).walls;
    MinMaxAccumulator<float> acc_x, acc_y;

    for (const auto &it : vertices) {
        acc_x.accumulate(it.x);
        acc_y.accumulate(it.y);
    }

    float xmin = acc_x.get_min();
    float xmax = acc_x.get_max();
    float ymin = acc_y.get_min();
    float ymax = acc_y.get_max();

    float board_width = (xmax - xmin) / 1e6;
    float board_height = (ymax - ymin) / 1e6;

    if (board_height < 1 || board_width < 1)
        return;

    center = {(xmin + xmax) / 2e6, (ymin + ymax) / 2e6};


    cam_distance = std::max(board_width / width, board_height / height) / (2 * magic_number / height) * 1.1;
    cam_azimuth = 270;
    cam_elevation = 89.99;
    queue_draw();
}

void Canvas3D::request_push()
{
    needs_push = true;
    queue_draw();
}

void Canvas3D::push()
{
    cover_renderer.push();
    wall_renderer.push();
    face_renderer.push();
}

void Canvas3D::resize_buffers()
{
    GLint rb;
    GLint samples = gl_clamp_samples(num_samples);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &rb); // save rb
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width * get_scale_factor(),
                                     height * get_scale_factor());
    glBindRenderbuffer(GL_RENDERBUFFER, depthrenderbuffer);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT, width * get_scale_factor(),
                                     height * get_scale_factor());
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
}

void Canvas3D::on_realize()
{
    Gtk::GLArea::on_realize();
    make_current();
    cover_renderer.realize();
    wall_renderer.realize();
    face_renderer.realize();
    background_renderer.realize();
    glEnable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLint fb;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb); // save fb

    glGenRenderbuffers(1, &renderbuffer);
    glGenRenderbuffers(1, &depthrenderbuffer);

    resize_buffers();

    GL_CHECK_ERROR

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthrenderbuffer);

    GL_CHECK_ERROR

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        Gtk::MessageDialog md("Error setting up framebuffer, will now exit", false /* use_markup */, Gtk::MESSAGE_ERROR,
                              Gtk::BUTTONS_OK);
        md.run();
        abort();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fb);

    GL_CHECK_ERROR
}

void Canvas3D::update2(const Board &b)
{
    needs_view_all = brd == nullptr;
    brd = &b;
    ca.update(*brd);
    prepare_packages();
    prepare();
}

void Canvas3D::prepare()
{
    bbox.first = glm::vec3();
    bbox.second = glm::vec3();
    for (const auto &it : ca.get_patches()) {
        for (const auto &path : it.second) {
            for (const auto &p : path) {
                glm::vec3 q(p.X / 1e6, p.Y / 1e6, 0);
                bbox.first = glm::min(bbox.first, q);
                bbox.second = glm::max(bbox.second, q);
            }
        }
    }
    request_push();
}

float Canvas3D::get_layer_offset(int layer) const
{
    if (layer == 20000)
        return get_layer_offset(BoardLayers::TOP_COPPER);

    else
        return ca.get_layer(layer).offset + ca.get_layer(layer).explode_mul * explode;
}

float Canvas3D::get_layer_thickness(int layer) const
{
    if (layer == BoardLayers::L_OUTLINE && explode == 0) {
        return ca.get_layer(BoardLayers::BOTTOM_COPPER).offset + ca.get_layer(BoardLayers::BOTTOM_COPPER).thickness;
    }
    else if (layer == 20000) {
        return -(get_layer_offset(BoardLayers::TOP_COPPER) - get_layer_offset(BoardLayers::BOTTOM_COPPER));
    }
    else {
        return ca.get_layer(layer).thickness;
    }
}
void Canvas3D::load_3d_model(const std::string &filename, const std::string &filename_abs)
{
    if (models.count(filename))
        return;

    auto faces = STEPImporter::import(filename_abs);
    // canvas->face_vertex_buffer.reserve(faces.size());
    size_t vertex_offset = face_vertex_buffer.size();
    size_t first_index = face_index_buffer.size();
    for (const auto &face : faces) {
        for (const auto &v : face.vertices) {
            face_vertex_buffer.emplace_back(v.x, v.y, v.z, face.color.r * 255, face.color.g * 255, face.color.b * 255);
        }
        for (const auto &tri : face.triangle_indices) {
            size_t a, b, c;
            std::tie(a, b, c) = tri;
            face_index_buffer.push_back(a + vertex_offset);
            face_index_buffer.push_back(b + vertex_offset);
            face_index_buffer.push_back(c + vertex_offset);
        }
        vertex_offset += face.vertices.size();
    }
    size_t last_index = face_index_buffer.size();
    models.emplace(std::piecewise_construct, std::forward_as_tuple(filename),
                   std::forward_as_tuple(first_index, last_index - first_index));
}

void Canvas3D::load_models_thread(std::map<std::string, std::string> model_filenames)
{
    std::lock_guard<std::mutex> lock(models_loading_mutex);
    for (const auto &it : model_filenames) {
        load_3d_model(it.first, it.second);
    }
    models_loading_dispatcher.emit();
}

void Canvas3D::load_models_async(Pool *pool)
{
    std::map<std::string, std::string> model_filenames; // first: relative, second: absolute
    for (const auto &it : brd->packages) {
        auto model = it.second.package.get_model(it.second.model);
        if (model) {
            std::string model_filename;
            const Package *pool_package = nullptr;

            UUID this_pool_uuid;
            UUID pkg_pool_uuid;
            auto base_path = pool->get_base_path();
            const auto &pools = PoolManager::get().get_pools();
            if (pools.count(base_path))
                this_pool_uuid = pools.at(base_path).uuid;


            try {
                pool_package = pool->get_package(it.second.pool_package->uuid, &pkg_pool_uuid);
            }
            catch (const std::runtime_error &e) {
                // it's okay
            }
            if (it.second.pool_package == pool_package) {
                // package is from pool, ask pool for model filename (might come from cache)
                model_filename = pool->get_model_filename(it.second.pool_package->uuid, model->uuid);
            }
            else {
                // package is not from pool (from package editor, use filename relative to current pool)
                if (pkg_pool_uuid && pkg_pool_uuid != this_pool_uuid) { // pkg is open in RO mode from included pool
                    model_filename = pool->get_model_filename(it.second.pool_package->uuid, model->uuid);
                }
                else { // really editing package
                    model_filename = Glib::build_filename(pool->get_base_path(), model->filename);
                }
            }
            if (model_filename.size())
                model_filenames[model->filename] = model_filename;
        }
    }
    s_signal_models_loading.emit(true);
    std::thread thr(&Canvas3D::load_models_thread, this, model_filenames);

    thr.detach();
}

void Canvas3D::clear_3d_models()
{
    face_vertex_buffer.clear();
    face_index_buffer.clear();
    models.clear();
}

void Canvas3D::update_packages()
{
    prepare_packages();
    request_push();
}

void Canvas3D::set_highlights(const std::set<UUID> &pkgs)
{
    packages_highlight = pkgs;
    update_packages();
}

void Canvas3D::prepare_packages()
{
    if (!brd)
        return;
    package_transform_idxs.clear();
    package_transforms.clear();
    std::map<std::string, std::set<const BoardPackage *>> pkg_map;
    for (const auto &it : brd->packages) {
        auto model = it.second.package.get_model(it.second.model);
        if (model)
            pkg_map[model->filename].insert(&it.second);
    }

    for (const auto &it_pkg : pkg_map) {
        size_t size_before = package_transforms.size();
        for (const auto &it_brd_pkg : it_pkg.second) {
            const auto &pl = it_brd_pkg->placement;
            const auto &pkg = it_brd_pkg->package;
            package_transforms.emplace_back(pl.shift.x / 1e6, pl.shift.y / 1e6, pl.get_angle(), it_brd_pkg->flip,
                                            packages_highlight.count(it_brd_pkg->uuid));
            auto &tr = package_transforms.back();
            const auto model = pkg.get_model(it_brd_pkg->model);
            if (model) {
                tr.model_x = model->x / 1e6;
                tr.model_y = model->y / 1e6;
                tr.model_z = model->z / 1e6;
                tr.model_roll = model->roll;
                tr.model_pitch = model->pitch;
                tr.model_yaw = model->yaw;
            }
        }
        size_t size_after = package_transforms.size();
        package_transform_idxs[it_pkg.first] = {size_before, size_after - size_before};
    }
}

void Canvas3D::set_msaa(unsigned int samples)
{
    num_samples = samples;
    needs_resize = true;
    queue_draw();
}

bool Canvas3D::layer_is_visible(int layer) const
{
    if (layer == 20000) // pth holes
        return true;

    if (layer == BoardLayers::TOP_MASK || layer == BoardLayers::BOTTOM_MASK)
        return show_solder_mask;

    if (layer == BoardLayers::TOP_PASTE || layer == BoardLayers::BOTTOM_PASTE)
        return show_solder_paste;

    if (layer == BoardLayers::TOP_SILKSCREEN || layer == BoardLayers::BOTTOM_SILKSCREEN)
        return show_silkscreen;

    if (layer == BoardLayers::L_OUTLINE || layer >= 10000) {
        if (show_substrate) {
            if (layer == BoardLayers::L_OUTLINE)
                return true;
            else {
                return explode > 0;
            }
        }
        else {
            return false;
        }
    }
    if (layer < BoardLayers::TOP_COPPER && layer > BoardLayers::BOTTOM_COPPER)
        return show_substrate == false || explode > 0;

    return true;
}

Color Canvas3D::get_layer_color(int layer) const
{
    if (layer == 20000 || BoardLayers::is_copper(layer)) { // pth or cu
        if (use_layer_colors && appearance.layer_colors.count(layer)) {
            return appearance.layer_colors.at(layer);
        }
        return {1, .8, 0};
    }

    if (layer == BoardLayers::TOP_MASK || layer == BoardLayers::BOTTOM_MASK)
        return solder_mask_color;

    if (layer == BoardLayers::TOP_PASTE || layer == BoardLayers::BOTTOM_PASTE)
        return {.7, .7, .7};

    if (layer == BoardLayers::TOP_SILKSCREEN || layer == BoardLayers::BOTTOM_SILKSCREEN)
        return {1, 1, 1};

    if (layer == BoardLayers::L_OUTLINE || layer >= 10000)
        return substrate_color;
    return {1, 0, 0};
}

bool Canvas3D::on_render(const Glib::RefPtr<Gdk::GLContext> &context)
{
    if (needs_push) {
        push();
        needs_push = false;
    }
    if (needs_resize) {
        resize_buffers();
        needs_resize = false;
    }

    GLint fb;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb); // save fb

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glClearColor(.5, .5, .5, 1.0);
    glClearDepth(10);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    GL_CHECK_ERROR

    glDisable(GL_DEPTH_TEST);
    background_renderer.render();
    glEnable(GL_DEPTH_TEST);

    float r = cam_distance;
    float phi = glm::radians(cam_azimuth);
    float theta = glm::radians(90 - cam_elevation);
    auto cam_offset = glm::vec3(r * sin(theta) * cos(phi), r * sin(theta) * sin(phi), r * cos(theta));
    auto cam_pos = cam_offset + glm::vec3(center, 0);

    glm::vec3 right(sin(phi - 3.14f / 2.0f), cos(phi - 3.14f / 2.0f), 0);

    viewmat = glm::lookAt(cam_pos, glm::vec3(center, 0), glm::vec3(0, 0, std::abs(cam_elevation) < 90 ? 1 : -1));

    float cam_dist_min = std::max(std::abs(cam_pos.z) - (10 + explode * (brd->get_n_inner_layers() * 2 + 3)), 1.0f);
    float cam_dist_max = 0;

    float zmin = -10 - explode * (brd->get_n_inner_layers() * 2 + 3 + package_height_max);
    float zmax = 10 + explode * 2 + package_height_max;
    std::array<glm::vec3, 8> bbs = {
            glm::vec3(bbox.first.x, bbox.first.y, zmin),  glm::vec3(bbox.first.x, bbox.second.y, zmin),
            glm::vec3(bbox.second.x, bbox.first.y, zmin), glm::vec3(bbox.second.x, bbox.second.y, zmin),
            glm::vec3(bbox.first.x, bbox.first.y, zmax),  glm::vec3(bbox.first.x, bbox.second.y, zmax),
            glm::vec3(bbox.second.x, bbox.first.y, zmax), glm::vec3(bbox.second.x, bbox.second.y, zmax)};

    for (const auto &bb : bbs) {
        float dist = glm::length(bb - cam_pos);
        cam_dist_max = std::max(dist, cam_dist_max);
        cam_dist_min = std::min(dist, cam_dist_min);
    }
    float m = magic_number / height * cam_distance;
    float d = cam_dist_max * 2;
    if (projection == Projection::PERSP) {
        projmat = glm::perspective(glm::radians(cam_fov), width / height, cam_dist_min / 2, cam_dist_max * 2);
    }
    else {
        projmat = glm::ortho(-width * m, width * m, -height * m, height * m, -d, d);
    }

    cam_normal = glm::normalize(cam_offset);
    wall_renderer.render();

    if (show_models)
        face_renderer.render();

    cover_renderer.render();

    GL_CHECK_ERROR

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBlitFramebuffer(0, 0, width * get_scale_factor(), height * get_scale_factor(), 0, 0, width * get_scale_factor(),
                      height * get_scale_factor(), GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    GL_CHECK_ERROR
    glFlush();

    return Gtk::GLArea::on_render(context);
}
} // namespace horizon
