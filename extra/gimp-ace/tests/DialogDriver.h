// Included only in the separate test binary, inside Plugin.cpp's namespace.
struct DialogDriver { GtkWidget* dialog; FormatControls* controls; bool cancel; };
gboolean driveDialog(gpointer data) {
    auto& test = *static_cast<DialogDriver*>(data);
    auto& ui = *test.controls;
    auto* combo = GTK_COMBO_BOX(ui.combo);
    auto rows = [&]() { return gtk_tree_model_iter_n_children(gtk_combo_box_get_model(combo), nullptr); };
    g_assert_cmpint(rows(), ==, 4); // auto, RGBA, DXT3, DXT5
    auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
        gtk_widget_get_allocated_width(test.dialog), gtk_widget_get_allocated_height(test.dialog));
    auto* cr = cairo_create(surface);
    gtk_widget_draw(test.dialog, cr);
    auto* path = g_build_filename(g_getenv("ACE_TEST_OUTPUT"), "export-dialog.png", nullptr);
    g_assert_cmpint(cairo_surface_write_to_png(surface, path), ==, CAIRO_STATUS_SUCCESS);
    cairo_destroy(cr); cairo_surface_destroy(surface); g_free(path);
    g_object_set(ui.config, "suggested-only", FALSE, "match-source", FALSE, nullptr);
    g_assert_cmpint(rows(), ==, 15);
    gtk_combo_box_set_active_id(combo, "dxt4");
    g_object_set(ui.config, "suggested-only", TRUE, nullptr);
    g_assert_cmpstr(gtk_combo_box_get_active_id(combo), ==, "auto");
    g_object_set(ui.config, "match-source", TRUE, nullptr);
    g_assert_cmpint(rows(), ==, 4);
    gtk_combo_box_set_active_id(combo, "rgba");
    g_object_set(ui.config, "mipmaps", TRUE, "zlib", TRUE, nullptr);
    gtk_dialog_response(GTK_DIALOG(test.dialog), test.cancel ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_OK);
    return G_SOURCE_REMOVE;
}
