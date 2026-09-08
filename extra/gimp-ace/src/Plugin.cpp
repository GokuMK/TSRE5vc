// GIMP 3 native ACE exporter. The codec uses std/miniz, not Qt.
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>
#include "Export.h"
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
constexpr const char* procedureName = "file-tsre-ace-export";
constexpr const char* binaryName = "file-tsre-ace";
struct AcePlugin { GimpPlugIn parent; };
struct AcePluginClass { GimpPlugInClass parent; };
GType ace_plugin_get_type();
G_DEFINE_TYPE(AcePlugin, ace_plugin, GIMP_TYPE_PLUG_IN)

struct Unref { void operator()(gpointer p) const { if (p) g_object_unref(p); } };
struct Free { void operator()(gpointer p) const { g_free(p); } };
struct PreparedImage {
    GimpImage* image;
    bool temporary = false;
    ~PreparedImage() { if (temporary) gimp_image_delete(image); }
};

struct FormatControls {
    GimpProcedureConfig* config;
    GtkComboBoxText* combo;
    bool sourceAlpha;
    bool updating = false;
};
void updateFormats(GObject*, GParamSpec*, gpointer data) {
    auto& ui = *static_cast<FormatControls*>(data);
    if (ui.updating) return;
    ui.updating = true;
    gboolean suggested = true, match = true;
    gchar* selected = nullptr;
    g_object_get(ui.config, "suggested-only", &suggested, "match-source", &match,
                 "encoding", &selected, nullptr);
    gtk_combo_box_text_remove_all(ui.combo);
    gtk_combo_box_text_append(ui.combo, "auto", ui.sourceAlpha ? "Automatic (RGBA)" : "Automatic (RGB)");
    for (const auto& f : AceExport::formats)
        if (AceExport::visible(f, suggested, match, ui.sourceAlpha))
            gtk_combo_box_text_append(ui.combo, f.id, f.label);
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(ui.combo), selected)) {
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(ui.combo), "auto");
        g_object_set(ui.config, "encoding", "auto", nullptr);
    }
    g_free(selected);
    ui.updating = false;
}
void formatChanged(GtkComboBox* combo, gpointer data) {
    auto& ui = *static_cast<FormatControls*>(data);
    if (!ui.updating) {
        const auto* id = gtk_combo_box_get_active_id(combo);
        if (id) g_object_set(ui.config, "encoding", id, nullptr);
    }
}
#ifdef ACE_GIMP_TEST_DIALOG
#include "../tests/DialogDriver.h"
bool testCancelDialog = false;
#endif
bool showDialog(GimpProcedure* procedure, GimpProcedureConfig* config,
                GimpImage* image, bool alpha) {
    gimp_ui_init(binaryName);
    auto* widget = gimp_export_procedure_dialog_new(GIMP_EXPORT_PROCEDURE(procedure), config, image);
    auto* dialog = GIMP_PROCEDURE_DIALOG(widget);
    auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(widget))), box, FALSE, FALSE, 0);
    auto* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    auto* label = gtk_label_new_with_mnemonic("ACE _encoding");
    auto* combo = gtk_combo_box_text_new();
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), combo);
    gtk_box_pack_start(GTK_BOX(row), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    FormatControls controls{config, GTK_COMBO_BOX_TEXT(combo), alpha};
    const gulong notify = g_signal_connect(config, "notify", G_CALLBACK(updateFormats), &controls);
    g_signal_connect(combo, "changed", G_CALLBACK(formatChanged), &controls);
    updateFormats(nullptr, nullptr, &controls);
    for (const char* name : {"suggested-only", "match-source", "mipmaps", "zlib"}) {
        auto* control = gimp_procedure_dialog_get_widget(dialog, name, G_TYPE_NONE);
        gtk_box_pack_start(GTK_BOX(box), control, FALSE, FALSE, 0);
    }
    auto* note = gtk_label_new("Exports the visible image as 8-bit sRGB.\n"
        "Mipmaps require a square, power-of-two image.\n"
        "Zlib compresses the ACE file independently of pixel encoding.");
    gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
    gtk_box_pack_start(GTK_BOX(box), note, FALSE, FALSE, 4);
    gtk_widget_show_all(box);
#ifdef ACE_GIMP_TEST_DIALOG
    DialogDriver driver{widget, &controls, testCancelDialog};
    g_timeout_add(500, driveDialog, &driver);
#endif
    const bool accepted = gimp_procedure_dialog_run(dialog);
    g_signal_handler_disconnect(config, notify);
    gtk_widget_destroy(widget);
    return accepted;
}

GimpValueArray* runExport(GimpProcedure* procedure, GimpRunMode mode, GimpImage* image,
                         GFile* file, GimpExportOptions* exportOptions, GimpMetadata*,
                         GimpProcedureConfig* config, gpointer) {
    GError* error = nullptr;
    try {
        gegl_init(nullptr, nullptr);
        PreparedImage prepared{image};
        prepared.temporary = gimp_export_options_get_image(exportOptions, &prepared.image) == GIMP_EXPORT_EXPORT;
        GList* layers = gimp_image_list_layers(prepared.image);
        auto* drawable = layers ? GIMP_DRAWABLE(layers->data) : nullptr;
        g_list_free(layers);
        if (!drawable) throw std::runtime_error("There is no visible layer to export.");
        const bool alpha = gimp_drawable_has_alpha(drawable);
#ifdef ACE_GIMP_TEST_DIALOG
        gchar* testName = g_file_get_basename(file);
        testCancelDialog = g_strcmp0(testName, "cancel.ace") == 0;
        g_free(testName);
#endif
        if (mode == GIMP_RUN_INTERACTIVE && !showDialog(procedure, config, prepared.image, alpha))
            return gimp_procedure_new_return_values(procedure, GIMP_PDB_CANCEL, nullptr);

        gboolean mipmaps = false, zlib = false;
        gchar* encodingString = nullptr;
        g_object_get(config, "encoding", &encodingString, "mipmaps", &mipmaps, "zlib", &zlib, nullptr);
        std::unique_ptr<gchar, Free> encoding(encodingString);
        const int width = gimp_image_get_width(prepared.image);
        const int height = gimp_image_get_height(prepared.image);
        std::string message;
        if (!AceExport::validateSize(width, height, mipmaps, message))
            throw std::runtime_error(message);
        gimp_progress_init("Exporting ACE texture");
        std::unique_ptr<GeglBuffer, Unref> buffer(gimp_drawable_get_buffer(drawable));
        if (!buffer) throw std::runtime_error("Could not read the image buffer.");
        std::vector<unsigned char> pixels(std::size_t(width) * height * 4);
        int offsetX = 0, offsetY = 0;
        gimp_drawable_get_offsets(drawable, &offsetX, &offsetY);
        const GeglRectangle rect{-offsetX, -offsetY, width, height};
        const Babl* format = babl_format_with_space("R'G'B'A u8", babl_space("sRGB"));
        gegl_buffer_get(buffer.get(), &rect, 1.0, format, pixels.data(), GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
        gimp_progress_update(0.25);
        QByteArray bytes;
        if (!AceExport::encode(pixels.data(), pixels.size(), width, height, alpha,
                               encoding.get(), mipmaps, zlib, bytes, message))
            throw std::runtime_error(message);
        gimp_progress_update(0.85);
        // GIO supports Unicode paths and remote destinations. Encode completely
        // before replacing; a codec failure cannot truncate an existing file.
        if (!g_file_replace_contents(file, bytes.constData(), static_cast<gsize>(bytes.size()),
                                     nullptr, FALSE, G_FILE_CREATE_NONE, nullptr, nullptr, &error))
            return gimp_procedure_new_return_values(procedure, GIMP_PDB_EXECUTION_ERROR, error);
        gimp_progress_update(1.0);
        return gimp_procedure_new_return_values(procedure, GIMP_PDB_SUCCESS, nullptr);
    } catch (const std::exception& e) {
        g_set_error_literal(&error, G_FILE_ERROR, G_FILE_ERROR_FAILED, e.what());
    } catch (...) {
        g_set_error_literal(&error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "Unexpected ACE export failure.");
    }
    return gimp_procedure_new_return_values(procedure, GIMP_PDB_EXECUTION_ERROR, error);
}
GList* queryProcedures(GimpPlugIn*) { return g_list_append(nullptr, g_strdup(procedureName)); }
gboolean setI18n(GimpPlugIn*, const gchar*, gchar**, gchar**) { return FALSE; }
GimpProcedure* createProcedure(GimpPlugIn* plugin, const gchar* name) {
    if (std::strcmp(name, procedureName)) return nullptr;
    auto* p = gimp_export_procedure_new(plugin, name, GIMP_PDB_PROC_TYPE_PLUGIN,
                                       FALSE, runExport, nullptr, nullptr);
    gimp_procedure_set_image_types(p, "*");
    gimp_procedure_set_menu_label(p, "MSTS / Open Rails ACE texture");
    gimp_procedure_set_documentation(p, "Export an ACE texture using the TSRE codec",
        "Exports the visible composite as 8-bit sRGB. Supports planar, packed, DXT and indexed ACE, "
        "optional mipmaps and zlib. Indexed output requires at most 256 colors. "
        "Mipmaps require square power-of-two dimensions. Does not modify the working image.", name);
    gimp_procedure_set_attribution(p, "TSRE contributors", "TSRE contributors", "2026");
    gimp_file_procedure_set_format_name(GIMP_FILE_PROCEDURE(p), "ACE");
    gimp_file_procedure_set_mime_types(GIMP_FILE_PROCEDURE(p), "image/x-msts-ace");
    gimp_file_procedure_set_extensions(GIMP_FILE_PROCEDURE(p), "ace");
    gimp_file_procedure_set_handles_remote(GIMP_FILE_PROCEDURE(p), TRUE);
    gimp_export_procedure_set_capabilities(GIMP_EXPORT_PROCEDURE(p),
        static_cast<GimpExportCapabilities>(GIMP_EXPORT_CAN_HANDLE_RGB | GIMP_EXPORT_CAN_HANDLE_ALPHA),
        nullptr, nullptr, nullptr);
    GimpChoice* choice = gimp_choice_new();
    gimp_choice_add(choice, "auto", 0, "Automatic (RGB / RGBA)", "Preserve the source alpha capability");
    int id = 1;
    for (const auto& f : AceExport::formats) gimp_choice_add(choice, f.id, id++, f.label, nullptr);
    gimp_procedure_add_choice_argument(p, "encoding", "ACE encoding", "Pixel storage format",
                                       choice, "auto", G_PARAM_READWRITE);
    gimp_procedure_add_boolean_argument(p, "mipmaps", "Generate _mipmaps",
        "Requires square power-of-two dimensions", FALSE, G_PARAM_READWRITE);
    gimp_procedure_add_boolean_argument(p, "zlib", "_Zlib compression",
        "Lossless ACE envelope compression", FALSE, G_PARAM_READWRITE);
    gimp_procedure_add_boolean_aux_argument(p, "suggested-only", "Suggested _OR / MSTS formats only",
        "Hide packed, indexed and premultiplied encodings", TRUE, G_PARAM_READWRITE);
    gimp_procedure_add_boolean_aux_argument(p, "match-source", "Match source image _format",
        "Show opaque formats for RGB; full-alpha formats for RGBA", TRUE, G_PARAM_READWRITE);
    return p;
}
void ace_plugin_class_init(AcePluginClass* klass) {
    auto* plugin = GIMP_PLUG_IN_CLASS(klass);
    plugin->query_procedures = queryProcedures;
    plugin->create_procedure = createProcedure;
    plugin->set_i18n = setI18n;
}
void ace_plugin_init(AcePlugin*) {}
}
// The Windows SDK macro names unused WinMain parameters.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
GIMP_MAIN(ace_plugin_get_type())
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
