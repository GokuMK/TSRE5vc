"""Run with GIMP's python-fu-eval in a disposable profile; see smoke.ps1."""
import os
from pathlib import Path
from gi.repository import Gimp, Gio, Gegl

output = Path(os.environ["ACE_TEST_OUTPUT"])
output.mkdir(parents=True, exist_ok=True)
procedure = Gimp.get_pdb().lookup_procedure("file-tsre-ace-export")
assert procedure is not None, "ACE exporter not registered"

def layer(image, name, width, height, color, alpha=True):
    kind = Gimp.ImageType.RGBA_IMAGE if alpha else Gimp.ImageType.RGB_IMAGE
    result = Gimp.Layer.new(image, name, width, height, kind, 100, Gimp.LayerMode.NORMAL)
    image.insert_layer(result, None, 0)
    Gimp.context_set_background(Gegl.Color.new(color))
    result.fill(Gimp.FillType.BACKGROUND)
    return result

def export(image, name, encoding="auto", mipmaps=False, zlib=False, success=True):
    config = procedure.create_config()
    config.set_property("run-mode", Gimp.RunMode.NONINTERACTIVE)
    config.set_property("image", image)
    config.set_property("file", Gio.File.new_for_path(str(output / name)))
    config.set_property("encoding", encoding)
    config.set_property("mipmaps", mipmaps)
    config.set_property("zlib", zlib)
    before = [(x.get_id(), x.get_name(), x.get_visible(), x.get_offsets()) for x in image.get_layers()]
    images_before = [x.get_id() for x in Gimp.get_images()]
    result = procedure.run(config)
    status = result.index(0)
    assert (status == Gimp.PDBStatusType.SUCCESS) == success, (name, status, result.index(1) if result.length() > 1 else "")
    assert before == [(x.get_id(), x.get_name(), x.get_visible(), x.get_offsets()) for x in image.get_layers()]
    assert images_before == [x.get_id() for x in Gimp.get_images()], "Temporary image leaked"

image = Gimp.Image.new(8, 8, Gimp.ImageBaseType.RGB)
base = layer(image, "red base", 8, 8, "red", False)
top = layer(image, "green patch", 4, 4, "lime")
top.set_offsets(2, 2)
hidden = layer(image, "hidden blue", 8, 8, "blue")
hidden.set_visible(False)
for encoding in ("auto", "rgb", "rgba", "mask", "rgb565", "argb1555", "argb4444",
                 "dxt1", "dxt1mask", "dxt2", "dxt3", "dxt4", "dxt5", "indexed-rgb", "indexed-rgba"):
    export(image, encoding + ".ace", encoding, True, True)
export(image, "plain.ace", "rgba")
image.delete()

alpha_image = Gimp.Image.new(8, 8, Gimp.ImageBaseType.RGB)
patch = layer(alpha_image, "offset patch", 4, 4, "red")
patch.set_offsets(2, 2)
export(alpha_image, "alpha.ace")
patch.set_opacity(50)
export(alpha_image, "half-alpha.ace")
alpha_image.delete()

offset_rgb = Gimp.Image.new(8, 8, Gimp.ImageBaseType.RGB)
patch = layer(offset_rgb, "opaque offset", 4, 4, "red", False)
patch.set_offsets(2, 2)
export(offset_rgb, "opaque-offset.ace")
offset_rgb.delete()

linear = Gimp.Image.new(8, 8, Gimp.ImageBaseType.RGB)
layer(linear, "color", 8, 8, "#804020", False)
linear.convert_precision(Gimp.Precision.FLOAT_LINEAR)
export(linear, "linear.ace")
assert linear.get_precision() == Gimp.Precision.FLOAT_LINEAR
linear.delete()

gray = Gimp.Image.new(8, 8, Gimp.ImageBaseType.GRAY)
gray_layer = Gimp.Layer.new(gray, "gray", 8, 8, Gimp.ImageType.GRAY_IMAGE, 100, Gimp.LayerMode.NORMAL)
gray.insert_layer(gray_layer, None, 0)
gray_layer.fill(Gimp.FillType.WHITE)
export(gray, "gray.ace")
assert gray.get_base_type() == Gimp.ImageBaseType.GRAY
gray.delete()

rectangle = Gimp.Image.new(8, 4, Gimp.ImageBaseType.RGB)
layer(rectangle, "rectangle", 8, 4, "red", False)
destination = output / "must-survive.ace"
destination.write_bytes(b"existing destination")
export(rectangle, destination.name, "rgba", mipmaps=True, success=False)
assert destination.read_bytes() == b"existing destination", "Failed export overwrote the destination"
export(rectangle, "missing-folder/out.ace", success=False)
rectangle.delete()
if os.environ.get("ACE_TEST_DIALOG"):
    image = Gimp.Image.new(8, 8, Gimp.ImageBaseType.RGB)
    layer(image, "dialog", 8, 8, "red")
    for cancel in (False, True):
        config = procedure.create_config()
        config.set_property("run-mode", Gimp.RunMode.INTERACTIVE)
        config.set_property("image", image)
        path = output / ("cancel.ace" if cancel else "dialog.ace")
        config.set_property("file", Gio.File.new_for_path(str(path)))
        result = procedure.run(config)
        expected = Gimp.PDBStatusType.CANCEL if cancel else Gimp.PDBStatusType.SUCCESS
        assert result.index(0) == expected, ("dialog", result.index(0))
        assert path.exists() != cancel
    image.delete()
(output / "PASS").write_text("GIMP registration, all encodings, compositing, alpha, gray, failures and image preservation passed\n")
print("ACE GIMP INTEGRATION PASS")
